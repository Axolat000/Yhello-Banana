#include "uspace.h"
#include "paging.h"
#include "kheap.h"
#include "kstring.h"
#include "spinlock.h"

#ifdef __x86_64__

#define P_PRESENT 0x001ull
#define P_WRITE   0x002ull
#define P_USER    0x004ull
#define P_LARGE   0x080ull
#define ADDR_MASK 0x000FFFFFFFFFF000ull
#define GIBS      512                   /* 1 GiB slots of PML4[0]: the first 512 GiB */

/* a gigabyte the app has memory in: a page directory of its own (the
 * kernel's entries copied) and which of its 2 MiB blocks are the app's */
typedef struct {
    uint64_t* pd;
    uint8_t   own[64];
} uslot_t;

struct uspace {
    uint64_t*      pml4;
    uint64_t*      pdpt;
    uslot_t*       slot[GIBS];          /* NULL: the kernel's directory, shared */
    struct uspace* next;
};

static uspace_t* g_all;                 /* every address space: kernel changes go to each */
/* the apps' tables: changed on the boot core (kernel changes, new apps) and
 * on the others (an app's memory growing in a system call made there) */
static spinlock_t g_lock = SPINLOCK_INIT;

static uint64_t* new_table(void) {
    uint64_t* t = (uint64_t*)kmalloc_aligned(4096, 4096);
    if (t) memset(t, 0, 4096);
    return t;
}

static int owned(const uslot_t* sl, uint32_t i) { return sl->own[i >> 3] & (1u << (i & 7)); }

static int is_current(const uspace_t* us) {
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (cr3 & ADDR_MASK) == (uintptr_t)us->pml4;
}

static void reload_cr3(void) {
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3) :: "memory");
}

static const uint64_t* kernel_pdpt(void) {
    const uint64_t* kpml4 = (const uint64_t*)(paging_kernel_cr3() & ADDR_MASK);
    return (const uint64_t*)(uintptr_t)(kpml4[0] & ADDR_MASK);
}

/* the kernel's entries of gigabyte k, into the app's directory for it */
static void copy_slot(uslot_t* sl, uint64_t kpdpte) {
    const uint64_t* kpd = (kpdpte & P_PRESENT) && !(kpdpte & P_LARGE) ? (const uint64_t*)(uintptr_t)(kpdpte & ADDR_MASK) : NULL;
    for (uint32_t i = 0; i < 512; i++)
        if (!owned(sl, i)) sl->pd[i] = kpd ? kpd[i] : 0;
}

/* the kernel's entries, into us: everything but the app's own blocks. The
 * levels above its blocks are open to user mode - each block's own entry
 * decides (the kernel's have no user bit) */
static void copy_kernel(uspace_t* us) {
    const uint64_t* kpml4 = (const uint64_t*)(paging_kernel_cr3() & ADDR_MASK);
    const uint64_t* kpdpt = kernel_pdpt();
    for (int i = 1; i < 512; i++) us->pml4[i] = kpml4[i];
    us->pml4[0] = (uint64_t)(uintptr_t)us->pdpt | P_PRESENT | P_WRITE | P_USER;
    for (int k = 0; k < GIBS; k++) {
        uslot_t* sl = us->slot[k];
        if (!sl) { us->pdpt[k] = kpdpt[k]; continue; }
        copy_slot(sl, kpdpt[k]);
        us->pdpt[k] = (uint64_t)(uintptr_t)sl->pd | P_PRESENT | P_WRITE | P_USER;
    }
}

/* the app's own directory for gigabyte k (made the first time) */
static uslot_t* slot_for(uspace_t* us, uint32_t k) {
    if (us->slot[k]) return us->slot[k];
    uslot_t* sl = (uslot_t*)kzalloc(sizeof(uslot_t));
    if (!sl) return NULL;
    sl->pd = new_table();
    if (!sl->pd) { kfree(sl); return NULL; }
    copy_slot(sl, kernel_pdpt()[k]);
    us->slot[k] = sl;
    us->pdpt[k] = (uint64_t)(uintptr_t)sl->pd | P_PRESENT | P_WRITE | P_USER;
    return sl;
}

uspace_t* uspace_create(void) {
    uspace_t* us = (uspace_t*)kzalloc(sizeof(uspace_t));
    if (!us) return NULL;
    us->pml4 = new_table();
    us->pdpt = new_table();
    if (!us->pml4 || !us->pdpt) {
        kfree(us->pml4); kfree(us->pdpt); kfree(us);
        return NULL;
    }
    uintptr_t f = spin_lock_irq(&g_lock);
    copy_kernel(us);
    us->next = g_all;
    g_all = us;
    spin_unlock_irq(&g_lock, f);
    return us;
}

void uspace_destroy(uspace_t* us) {
    if (!us) return;
    uintptr_t f = spin_lock_irq(&g_lock);
    for (uspace_t** pp = &g_all; *pp; pp = &(*pp)->next)
        if (*pp == us) { *pp = us->next; break; }
    spin_unlock_irq(&g_lock, f);
    for (int k = 0; k < GIBS; k++) {
        uslot_t* sl = us->slot[k];
        if (!sl) continue;
        for (uint32_t i = 0; i < 512; i++)
            if (owned(sl, i)) kfree((void*)(uintptr_t)(sl->pd[i] & ADDR_MASK));
        kfree(sl->pd);
        kfree(sl);
    }
    kfree(us->pml4);
    kfree(us->pdpt);
    kfree(us);
}

uintptr_t uspace_cr3(uspace_t* us) { return us ? (uintptr_t)us->pml4 : 0; }

int uspace_map(uspace_t* us, uintptr_t addr, uint32_t size) {
    if (!us || (addr & 0x1FFFFF) || (size & 0x1FFFFF) || (uint64_t)addr + size > ((uint64_t)GIBS << 30)) return -1;
    int rc = 0;
    uintptr_t f = spin_lock_irq(&g_lock);
    for (uintptr_t a = addr; a < addr + size; a += 0x200000) {
        uslot_t* sl = slot_for(us, (uint32_t)(a >> 30));
        if (!sl) { rc = -1; break; }
        uint32_t i = (uint32_t)((a >> 21) & 511);
        if (owned(sl, i)) continue;
        uint64_t* pt = new_table();
        if (!pt) { rc = -1; break; }
        for (int j = 0; j < 512; j++) pt[j] = ((uint64_t)a + ((uint64_t)j << 12)) | P_PRESENT | P_WRITE | P_USER;
        sl->own[i >> 3] |= (uint8_t)(1u << (i & 7));
        sl->pd[i] = (uint64_t)(uintptr_t)pt | P_PRESENT | P_WRITE | P_USER;
    }
    if (is_current(us)) reload_cr3();
    spin_unlock_irq(&g_lock, f);
    return rc;
}

void uspace_guard(uspace_t* us, uintptr_t addr, uint32_t size, int guard) {
    if (!us) return;
    int cur = is_current(us);
    uintptr_t f = spin_lock_irq(&g_lock);
    for (uintptr_t a = addr & ~(uintptr_t)4095; a < addr + size; a += 4096) {
        uint64_t k = a >> 30;
        uslot_t* sl = k < GIBS ? us->slot[k] : NULL;
        uint32_t i = (uint32_t)((a >> 21) & 511);
        if (!sl || !owned(sl, i)) continue;
        uint64_t* pt = (uint64_t*)(uintptr_t)(sl->pd[i] & ADDR_MASK);
        uint64_t* e = &pt[(a >> 12) & 511];
        if (guard) *e &= ~P_PRESENT;
        else *e |= P_PRESENT;
        if (cur) __asm__ volatile("invlpg (%0)" :: "r"(a) : "memory");
    }
    spin_unlock_irq(&g_lock, f);
}

void uspace_kernel_changed(void) {
    uintptr_t f = spin_lock_irq(&g_lock);
    for (uspace_t* us = g_all; us; us = us->next) copy_kernel(us);
    spin_unlock_irq(&g_lock, f);
}

int uspace_fault_is_stale(uintptr_t addr, uint32_t err) {
    if (!(err & 4)) return 0;                           /* not from user mode */
    uint64_t need = P_PRESENT | P_USER | ((err & 2) ? P_WRITE : 0);
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    const uint64_t* t = (const uint64_t*)(cr3 & ADDR_MASK);
    for (int shift = 39; shift >= 12; shift -= 9) {
        uint64_t e = t[(addr >> shift) & 511];
        if ((e & need) != need) return 0;
        if (shift == 12 || (shift < 39 && (e & P_LARGE))) break;
        t = (const uint64_t*)(uintptr_t)(e & ADDR_MASK);
    }
    __asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
    return 1;
}

#else

uspace_t* uspace_create(void) { return NULL; }
void      uspace_destroy(uspace_t* us) { (void)us; }
uintptr_t uspace_cr3(uspace_t* us) { (void)us; return 0; }
int       uspace_map(uspace_t* us, uintptr_t addr, uint32_t size) { (void)us; (void)addr; (void)size; return 0; }
void      uspace_guard(uspace_t* us, uintptr_t addr, uint32_t size, int guard) { (void)us; (void)addr; (void)size; (void)guard; }
void      uspace_kernel_changed(void) {}
int       uspace_fault_is_stale(uintptr_t addr, uint32_t err) { (void)addr; (void)err; return 0; }

#endif
