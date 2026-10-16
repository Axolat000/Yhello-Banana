#include "uspace.h"
#include "uvm.h"
#include "paging.h"
#include "kheap.h"
#include "kstring.h"
#include "spinlock.h"
#include "serial.h"

#ifdef __x86_64__

#define P_PRESENT 0x001ull
#define P_WRITE   0x002ull
#define P_USER    0x004ull
#define P_LARGE   0x080ull
#define P_KEPT    0x200ull              /* (a free bit) a mapped page made inaccessible: PROT_NONE */
#define ADDR_MASK 0x000FFFFFFFFFF000ull
#define GIBS      512                   /* 1 GiB slots of PML4[0]: the first 512 GiB */
#define UVM_PML4  ((int)(UVM_BASE >> 39))

/* a range of the mmap region: [start, end), its UVM_* protection */
typedef struct { uint64_t start, end; int prot; } uvma_t;

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
    /* the mmap region (uvm_*): its page directory pointer tables, the
     * ranges mapped (sorted), the pages in use */
    uint64_t*      vm_pdpt[UVM_SLOTS];
    uvma_t*        vma;
    int            nvma, vcap;
    uint64_t       vm_pages;
    spinlock_t     vm_lock;             /* its threads fault pages in on any core */
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
    for (int i = 1; i < 512; i++) {
        if (i >= UVM_PML4 && i < UVM_PML4 + UVM_SLOTS) {        /* the app's mmap region */
            uint64_t* t = us->vm_pdpt[i - UVM_PML4];
            us->pml4[i] = t ? ((uint64_t)(uintptr_t)t | P_PRESENT | P_WRITE | P_USER) : 0;
            continue;
        }
        us->pml4[i] = kpml4[i];
    }
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
    /* the mmap region's tables (its pages are the app's memory: umem frees them) */
    for (int s = 0; s < UVM_SLOTS; s++) {
        uint64_t* pdpt = us->vm_pdpt[s];
        if (!pdpt) continue;
        for (int i = 0; i < 512; i++) {
            if (!(pdpt[i] & P_PRESENT)) continue;
            uint64_t* pd = (uint64_t*)(uintptr_t)(pdpt[i] & ADDR_MASK);
            for (int j = 0; j < 512; j++)
                if (pd[j] & P_PRESENT) kfree((void*)(uintptr_t)(pd[j] & ADDR_MASK));
            kfree(pd);
        }
        kfree(pdpt);
    }
    kfree(us->vma);
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

/* ── the mmap region (uvm.h) ──────────────────────────────────────────
 * Its tables are the app's alone (PML4 slots UVM_PML4..): a page is a
 * present entry once touched, or a P_KEPT one while PROT_NONE hides it.
 * Another core running a thread of the same app may keep an old
 * translation of a page unmapped here until its time slice ends (its
 * tables are loaded again then): the page stays the app's memory, so at
 * worst the app scribbles on its own memory, as it asked for. */

static void invlpg(uint64_t a) { __asm__ volatile("invlpg (%0)" :: "r"((uintptr_t)a) : "memory"); }

static int in_region(uint64_t a, uint64_t n) {
    return a >= UVM_BASE && n <= UVM_SIZE && a - UVM_BASE <= UVM_SIZE - n;
}

static uint64_t prot_bits(int prot) {
    if (!(prot & (UVM_READ | UVM_WRITE | UVM_EXEC))) return P_KEPT;
    return P_PRESENT | P_USER | ((prot & UVM_WRITE) ? P_WRITE : 0);
}

/* the next level's table under entry *e (made if make): NULL if none */
static uint64_t* vm_next(uint64_t* e, int make) {
    if (!(*e & P_PRESENT)) {
        uint64_t* t;
        if (!make || !(t = new_table())) return NULL;
        *e = (uint64_t)(uintptr_t)t | P_PRESENT | P_WRITE | P_USER;
    }
    return (uint64_t*)(uintptr_t)(*e & ADDR_MASK);
}

/* the page table entry of va (its tables made if make), or NULL; *skip:
 * where the next entry that may exist is, when there is none */
static uint64_t* vm_pte(uspace_t* us, uint64_t va, int make, uint64_t* skip) {
    int s = (int)((va >> 39) & 511) - UVM_PML4;
    if (s < 0 || s >= UVM_SLOTS) return NULL;
    if (!us->vm_pdpt[s]) {
        uint64_t* t;
        if (!make || !(t = new_table())) { if (skip) *skip = (va | ((1ull << 39) - 1)) + 1; return NULL; }
        uintptr_t f = spin_lock_irq(&g_lock);           /* (copy_kernel writes the PML4 too) */
        us->vm_pdpt[s] = t;
        us->pml4[UVM_PML4 + s] = (uint64_t)(uintptr_t)t | P_PRESENT | P_WRITE | P_USER;
        spin_unlock_irq(&g_lock, f);
    }
    uint64_t* pd = vm_next(&us->vm_pdpt[s][(va >> 30) & 511], make);
    if (!pd) { if (skip) *skip = (va | ((1ull << 30) - 1)) + 1; return NULL; }
    uint64_t* pt = vm_next(&pd[(va >> 21) & 511], make);
    if (!pt) { if (skip) *skip = (va | ((1ull << 21) - 1)) + 1; return NULL; }
    return &pt[(va >> 12) & 511];
}

/* the range containing a, or -1 */
static int vma_at(uspace_t* us, uint64_t a) {
    int lo = 0, hi = us->nvma - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (a < us->vma[mid].start) hi = mid - 1;
        else if (a >= us->vma[mid].end) lo = mid + 1;
        else return mid;
    }
    return -1;
}

static int vma_room(uspace_t* us, int extra) {
    if (us->nvma + extra <= us->vcap) return 0;
    int cap = us->vcap ? us->vcap * 2 : 64;
    while (cap < us->nvma + extra) cap *= 2;
    uvma_t* n = (uvma_t*)kmalloc((size_t)cap * sizeof(uvma_t));
    if (!n) return -1;
    if (us->nvma) memcpy(n, us->vma, (size_t)us->nvma * sizeof(uvma_t));
    kfree(us->vma);
    us->vma = n;
    us->vcap = cap;
    return 0;
}

static void vma_del(uspace_t* us, int i) {
    memmove(&us->vma[i], &us->vma[i + 1], (size_t)(us->nvma - i - 1) * sizeof(uvma_t));
    us->nvma--;
}

/* [s, e) out of the ranges (one may split in two) */
static int vma_cut(uspace_t* us, uint64_t s, uint64_t e) {
    if (vma_room(us, 1)) return -1;
    for (int i = 0; i < us->nvma; ) {
        uvma_t* v = &us->vma[i];
        if (v->end <= s || v->start >= e) { i++; continue; }
        if (v->start < s && v->end > e) {
            memmove(&us->vma[i + 2], &us->vma[i + 1], (size_t)(us->nvma - i - 1) * sizeof(uvma_t));
            us->vma[i + 1] = (uvma_t){ e, v->end, v->prot };
            v->end = s;
            us->nvma++;
            return 0;
        }
        if (v->start < s) { v->end = s; i++; continue; }
        if (v->end > e) { v->start = e; i++; continue; }
        vma_del(us, i);
    }
    return 0;
}

/* a range where nothing is: joined to its neighbours of the same protection */
static int vma_add(uspace_t* us, uint64_t s, uint64_t e, int prot) {
    if (vma_room(us, 1)) return -1;
    int i = 0;
    while (i < us->nvma && us->vma[i].start < s) i++;
    if (i > 0 && us->vma[i - 1].end == s && us->vma[i - 1].prot == prot) {
        us->vma[i - 1].end = e;
        if (i < us->nvma && us->vma[i].start == e && us->vma[i].prot == prot) {
            us->vma[i - 1].end = us->vma[i].end;
            vma_del(us, i);
        }
        return 0;
    }
    if (i < us->nvma && us->vma[i].start == e && us->vma[i].prot == prot) { us->vma[i].start = s; return 0; }
    memmove(&us->vma[i + 1], &us->vma[i], (size_t)(us->nvma - i) * sizeof(uvma_t));
    us->vma[i] = (uvma_t){ s, e, prot };
    us->nvma++;
    return 0;
}

/* 1 if every byte of [s, e) is mapped (with all of need's protection bits) */
static int vma_covers(uspace_t* us, uint64_t s, uint64_t e, int need) {
    uint64_t a = s;
    while (a < e) {
        int i = vma_at(us, a);
        if (i < 0 || (us->vma[i].prot & need) != need) return 0;
        a = us->vma[i].end;
    }
    return 1;
}

static int vma_overlaps(uspace_t* us, uint64_t s, uint64_t e) {
    for (int i = 0; i < us->nvma; i++)
        if (us->vma[i].start < e && us->vma[i].end > s) return 1;
    return 0;
}

/* free addresses for len bytes: the hint if it is free, else the first gap */
static uint64_t vm_find(uspace_t* us, uint64_t len, uint64_t hint) {
    if (hint && !(hint & 4095) && in_region(hint, len) && !vma_overlaps(us, hint, hint + len)) return hint;
    uint64_t a = UVM_BASE;
    for (int i = 0; i < us->nvma; i++) {
        if (us->vma[i].start >= a && us->vma[i].start - a >= len) break;
        if (us->vma[i].end > a) a = us->vma[i].end;
    }
    return in_region(a, len) ? a : 0;
}

/* the pages of [s, e) back to the app's memory, their entries cleared */
static void vm_release(uspace_t* us, umem_t* m, uint64_t s, uint64_t e) {
    int cur = is_current(us);
    for (uint64_t a = s; a < e; ) {
        uint64_t next = a + 4096;
        uint64_t* pte = vm_pte(us, a, 0, &next);
        if (pte && (*pte & (P_PRESENT | P_KEPT))) {
            umem_free_pages(m, (void*)(uintptr_t)(*pte & ADDR_MASK), 0);
            us->vm_pages--;
            *pte = 0;
            if (cur) invlpg(a);
        }
        a = next;
    }
}

uintptr_t uvm_map(uspace_t* us, umem_t* m, uintptr_t addr, uint64_t len, int prot, int flags) {
    if (!us || !m || !len || len > UVM_SIZE) return 0;
    len = (len + 4095) & ~4095ull;
    prot &= UVM_READ | UVM_WRITE | UVM_EXEC;
    uintptr_t f = spin_lock_irq(&us->vm_lock);
    uint64_t a = 0;
    if (flags & UVM_FIXED) {
        if (!(addr & 4095) && in_region(addr, len) && vma_cut(us, addr, addr + len) == 0) {
            vm_release(us, m, addr, addr + len);          /* (what was there is gone: fresh pages) */
            a = addr;
        }
    } else a = vm_find(us, len, addr);
    if (a && vma_add(us, a, a + len, prot)) a = 0;
    spin_unlock_irq(&us->vm_lock, f);
    return (uintptr_t)a;
}

int uvm_unmap(uspace_t* us, umem_t* m, uintptr_t addr, uint64_t len) {
    if (!us || (addr & 4095) || !len) return -1;
    len = (len + 4095) & ~4095ull;
    if (!in_region(addr, len)) return -1;
    uintptr_t f = spin_lock_irq(&us->vm_lock);
    int rc = vma_cut(us, addr, addr + len);
    if (rc == 0) vm_release(us, m, addr, addr + len);
    spin_unlock_irq(&us->vm_lock, f);
    return rc;
}

int uvm_protect(uspace_t* us, uintptr_t addr, uint64_t len, int prot) {
    if (!us || (addr & 4095) || !len) return -1;
    len = (len + 4095) & ~4095ull;
    prot &= UVM_READ | UVM_WRITE | UVM_EXEC;
    if (!in_region(addr, len)) return -1;
    uintptr_t f = spin_lock_irq(&us->vm_lock);
    int rc = -1;
    if (vma_covers(us, addr, addr + len, 0) && vma_cut(us, addr, addr + len) == 0 && vma_add(us, addr, addr + len, prot) == 0) {
        int cur = is_current(us);
        for (uint64_t a = addr; a < addr + len; ) {
            uint64_t next = a + 4096;
            uint64_t* pte = vm_pte(us, a, 0, &next);
            if (pte && (*pte & (P_PRESENT | P_KEPT))) {
                *pte = (*pte & ADDR_MASK) | prot_bits(prot);
                if (cur) invlpg(a);
            }
            a = next;
        }
        rc = 0;
    }
    spin_unlock_irq(&us->vm_lock, f);
    return rc;
}

int uvm_owns(uspace_t* us, const void* p, uint64_t n) {
    uint64_t a = (uint64_t)(uintptr_t)p;
    if (!us || !n) n = 1;
    if (!us || !in_region(a, n)) return 0;
    uintptr_t f = spin_lock_irq(&us->vm_lock);
    int ok = vma_covers(us, a, a + n, UVM_READ) || vma_covers(us, a, a + n, UVM_WRITE);
    spin_unlock_irq(&us->vm_lock, f);
    return ok;
}

int uvm_owns_str(uspace_t* us, const char* s) {
    uint64_t a = (uint64_t)(uintptr_t)s;
    if (!us || !in_region(a, 1)) return 0;
    for (;;) {
        /* the page must be readable; reading it may fault it in (uvm_fault),
         * so the lock is not held then */
        if (!uvm_owns(us, (const void*)(uintptr_t)a, 1)) return 0;
        uint64_t end = (a | 4095) + 1;
        for (; a < end; a++) if (!*(const volatile char*)(uintptr_t)a) return 1;
        if (a - (uint64_t)(uintptr_t)s > (1u << 20)) return 0;       /* (a string, not a megabyte) */
    }
}

int uvm_fault(uspace_t* us, umem_t* m, uintptr_t addr, uint32_t err) {
    if (!us || !m || !in_region(addr, 1)) return 0;
    uintptr_t f = spin_lock_irq(&us->vm_lock);
    int ok = 0, i = vma_at(us, addr);
    int prot = i >= 0 ? us->vma[i].prot : 0;
    if (prot && (!(err & 2) || (prot & UVM_WRITE))) {
        uint64_t* pte = vm_pte(us, addr & ~4095ull, 1, NULL);
        if (pte && (*pte & P_PRESENT)) {
            invlpg(addr);                                  /* another thread put it there first */
            ok = 1;
        } else if (pte && !(*pte & P_KEPT)) {
            void* pg = umem_alloc_pages(m, 4096, 0);
            if (pg) {
                memset(pg, 0, 4096);
                *pte = (uint64_t)(uintptr_t)pg | prot_bits(prot);
                us->vm_pages++;
                ok = 1;
            }
        }
    }
    spin_unlock_irq(&us->vm_lock, f);
    return ok;
}

uint64_t uvm_resident(uspace_t* us) { return us ? us->vm_pages * 4096 : 0; }

#else

uspace_t* uspace_create(void) { return NULL; }
void      uspace_destroy(uspace_t* us) { (void)us; }
uintptr_t uspace_cr3(uspace_t* us) { (void)us; return 0; }
int       uspace_map(uspace_t* us, uintptr_t addr, uint32_t size) { (void)us; (void)addr; (void)size; return 0; }
void      uspace_guard(uspace_t* us, uintptr_t addr, uint32_t size, int guard) { (void)us; (void)addr; (void)size; (void)guard; }
void      uspace_kernel_changed(void) {}
int       uspace_fault_is_stale(uintptr_t addr, uint32_t err) { (void)addr; (void)err; return 0; }
/* (no mmap region: kernel/app.c maps app memory directly) */
int       uvm_owns(uspace_t* us, const void* p, uint64_t n) { (void)us; (void)p; (void)n; return 0; }
int       uvm_owns_str(uspace_t* us, const char* s) { (void)us; (void)s; return 0; }
int       uvm_fault(uspace_t* us, umem_t* m, uintptr_t addr, uint32_t err) { (void)us; (void)m; (void)addr; (void)err; return 0; }
uint64_t  uvm_resident(uspace_t* us) { (void)us; return 0; }

#endif
