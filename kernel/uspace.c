#include "uspace.h"
#include "paging.h"
#include "kheap.h"
#include "kstring.h"

#ifdef __x86_64__

#define P_PRESENT 0x001ull
#define P_WRITE   0x002ull
#define P_USER    0x004ull
#define P_LARGE   0x080ull
#define ADDR_MASK 0x000FFFFFFFFFF000ull
#define FRAMES    2048                  /* 2 MiB blocks: the first 4 GiB */

struct uspace {
    uint64_t*      pml4;
    uint64_t*      pdpt;
    uint64_t*      pd;                  /* 4 directories in a row: the first 4 GiB */
    uint8_t        own[FRAMES / 8];     /* the app's blocks: their entry points at a table of its own */
    struct uspace* next;
};

static uspace_t* g_all;                 /* every address space: kernel changes go to each */

static uint64_t* new_table(size_t bytes) {
    uint64_t* t = (uint64_t*)kmalloc_aligned(bytes, 4096);
    if (t) memset(t, 0, bytes);
    return t;
}

static int owned(const uspace_t* us, uint32_t f) { return us->own[f >> 3] & (1u << (f & 7)); }

static int is_current(const uspace_t* us) {
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (cr3 & ADDR_MASK) == (uintptr_t)us->pml4;
}

static void reload_cr3(void) {
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3) :: "memory");
}

/* the kernel's entries, into us: everything but the app's own blocks. The
 * levels above the blocks are open to user mode - each block's own entry
 * decides (the kernel's have no user bit) */
static void copy_kernel(uspace_t* us) {
    const uint64_t* kpml4 = (const uint64_t*)(paging_kernel_cr3() & ADDR_MASK);
    const uint64_t* kpdpt = (const uint64_t*)(uintptr_t)(kpml4[0] & ADDR_MASK);
    for (int i = 1; i < 512; i++) us->pml4[i] = kpml4[i];
    us->pml4[0] = (uint64_t)(uintptr_t)us->pdpt | P_PRESENT | P_WRITE | P_USER;
    for (int i = 4; i < 512; i++) us->pdpt[i] = kpdpt[i];
    for (int k = 0; k < 4; k++) {
        us->pdpt[k] = (uint64_t)(uintptr_t)(us->pd + 512 * k) | P_PRESENT | P_WRITE | P_USER;
        const uint64_t* kpd = (const uint64_t*)(uintptr_t)(kpdpt[k] & ADDR_MASK);
        for (int i = 0; i < 512; i++) {
            uint32_t f = (uint32_t)(k * 512 + i);
            if (!owned(us, f)) us->pd[f] = kpd[i];
        }
    }
}

uspace_t* uspace_create(void) {
    uspace_t* us = (uspace_t*)kzalloc(sizeof(uspace_t));
    if (!us) return NULL;
    us->pml4 = new_table(4096);
    us->pdpt = new_table(4096);
    us->pd = new_table(4 * 4096);
    if (!us->pml4 || !us->pdpt || !us->pd) {
        kfree(us->pml4); kfree(us->pdpt); kfree(us->pd); kfree(us);
        return NULL;
    }
    copy_kernel(us);
    us->next = g_all;
    g_all = us;
    return us;
}

void uspace_destroy(uspace_t* us) {
    if (!us) return;
    for (uspace_t** pp = &g_all; *pp; pp = &(*pp)->next)
        if (*pp == us) { *pp = us->next; break; }
    for (uint32_t f = 0; f < FRAMES; f++)
        if (owned(us, f)) kfree((void*)(uintptr_t)(us->pd[f] & ADDR_MASK));
    kfree(us->pml4);
    kfree(us->pdpt);
    kfree(us->pd);
    kfree(us);
}

uintptr_t uspace_cr3(uspace_t* us) { return us ? (uintptr_t)us->pml4 : 0; }

int uspace_map(uspace_t* us, uintptr_t addr, uint32_t size) {
    if (!us || (addr & 0x1FFFFF) || (size & 0x1FFFFF) || addr + size > (4ull << 30)) return -1;
    for (uintptr_t a = addr; a < addr + size; a += 0x200000) {
        uint32_t f = (uint32_t)(a >> 21);
        if (owned(us, f)) continue;
        uint64_t* pt = new_table(4096);
        if (!pt) return -1;
        for (int i = 0; i < 512; i++) pt[i] = ((uint64_t)a + ((uint64_t)i << 12)) | P_PRESENT | P_WRITE | P_USER;
        us->pd[f] = (uint64_t)(uintptr_t)pt | P_PRESENT | P_WRITE | P_USER;
        us->own[f >> 3] |= (uint8_t)(1u << (f & 7));
    }
    if (is_current(us)) reload_cr3();
    return 0;
}

void uspace_guard(uspace_t* us, uintptr_t addr, uint32_t size, int guard) {
    if (!us) return;
    int cur = is_current(us);
    for (uintptr_t a = addr & ~(uintptr_t)4095; a < addr + size; a += 4096) {
        uint32_t f = (uint32_t)(a >> 21);
        if (f >= FRAMES || !owned(us, f)) continue;
        uint64_t* pt = (uint64_t*)(uintptr_t)(us->pd[f] & ADDR_MASK);
        uint64_t* e = &pt[(a >> 12) & 511];
        if (guard) *e &= ~P_PRESENT;
        else *e |= P_PRESENT;
        if (cur) __asm__ volatile("invlpg (%0)" :: "r"(a) : "memory");
    }
}

void uspace_kernel_changed(void) {
    for (uspace_t* us = g_all; us; us = us->next) copy_kernel(us);
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
