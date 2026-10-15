#include "appmem.h"
#include "kheap.h"
#include "kstring.h"
#include "serial.h"

uintptr_t g_kernel_cr3;
static uintptr_t g_fault_base;
static uint32_t  g_fault_size;

/* a block in a chunk: blocks tile the chunk in address order, like the
 * kernel heap's - but these headers are in the app's memory */
typedef struct ablk {
    uint32_t     size;              /* payload bytes */
    uint32_t     used;              /* 0 free, AMAGIC in use */
    struct ablk* prev;
    struct ablk* next;
} __attribute__((aligned(16))) ablk_t;

#define AHDR   ((uint32_t)sizeof(ablk_t))
#define AMAGIC 0xA9917E57u
#define AMIN   64u                  /* no slivers smaller than this */

/* the chunk records are the kernel's (kmalloc), out of the app's reach */
typedef struct chunk {
    struct chunk* next;
    uintptr_t     base;
    uint32_t      size;
} chunk_t;

struct appmem {
    chunk_t*  chunks;
    uint32_t  used;
#ifdef __x86_64__
    uint64_t* pml4;
    uint64_t* pdpt;                 /* for the first 512 GiB (the kernel's PML4 entry 0) */
    uint64_t* pd[4];                /* copies of the kernel's for the GiBs with chunks */
#endif
};

static uint32_t up(uint32_t v, uint32_t a) { return (v + a - 1u) & ~(a - 1u); }

/* b is a believable header in chunk c: inside it, aligned, and tiling it */
static int blk_ok(const chunk_t* c, const ablk_t* b) {
    uintptr_t a = (uintptr_t)b, end = c->base + c->size;
    if (a < c->base || a + AHDR > end || (a & 15)) return 0;
    if (b->used != 0 && b->used != AMAGIC) return 0;
    if (b->size > c->size || a + AHDR + b->size > end) return 0;
    if (b->next ? (uintptr_t)b->next != a + AHDR + b->size : a + AHDR + b->size != end) return 0;
    if (b->prev ? ((uintptr_t)b->prev < c->base || (uintptr_t)b->prev >= a) : a != c->base) return 0;
    return 1;
}

static chunk_t* chunk_of(appmem_t* m, uintptr_t a) {
    for (chunk_t* c = m->chunks; c; c = c->next)
        if (a >= c->base && a < c->base + c->size) return c;
    return NULL;
}

/* ── the app's page tables (64-bit) ───────────────────────────────── */
#ifdef __x86_64__
#define PT_P     0x001ull
#define PT_W     0x002ull
#define PT_LARGE 0x080ull
#define PT_ADDR  0x000FFFFFFFFFF000ull

static uint64_t* tbl(uint64_t e) { return (uint64_t*)(uintptr_t)(e & PT_ADDR); }

static uint64_t* new_table(void) {
    uint64_t* t = (uint64_t*)kmalloc_aligned(4096, 4096);
    if (t) memset(t, 0, 4096);
    return t;
}

/* the kernel's page directory for GiB gi (below 4 GiB), NULL if it has none */
static uint64_t* kernel_pd(int gi) {
    uint64_t* pml4 = tbl(g_kernel_cr3);
    if (!(pml4[0] & PT_P)) return NULL;
    uint64_t* pdpt = tbl(pml4[0]);
    if (!(pdpt[gi] & PT_P) || (pdpt[gi] & PT_LARGE)) return NULL;
    return tbl(pdpt[gi]);
}

/* everything the kernel maps, read-only */
static int build(appmem_t* m) {
    m->pml4 = new_table();
    m->pdpt = new_table();
    if (!m->pml4 || !m->pdpt) return -1;
    uint64_t* kp = tbl(g_kernel_cr3);
    for (int i = 0; i < 512; i++) m->pml4[i] = kp[i] & ~PT_W;
    if (kp[0] & PT_P) {
        uint64_t* kd = tbl(kp[0]);
        for (int i = 0; i < 512; i++) m->pdpt[i] = kd[i] & ~PT_W;
        m->pml4[0] = (uint64_t)(uintptr_t)m->pdpt | (kp[0] & 0xFFFull) | PT_W;
    }
    return 0;
}

/* the app's own copy of the page directory for GiB gi, made writable at
 * the level above (each 2 MiB entry says for itself) */
static uint64_t* app_pd(appmem_t* m, int gi) {
    if (gi < 0 || gi >= 4) return NULL;
    if (m->pd[gi]) return m->pd[gi];
    uint64_t* kpd = kernel_pd(gi);
    if (!kpd || !m->pdpt) return NULL;
    uint64_t* pd = new_table();
    if (!pd) return NULL;
    for (int i = 0; i < 512; i++) pd[i] = kpd[i] & ~PT_W;
    for (uintptr_t a = g_fault_base & ~(uintptr_t)0x1FFFFF; a < g_fault_base + g_fault_size; a += 0x200000)
        if ((int)(a >> 30) == gi) pd[(a >> 21) & 511] = kpd[(a >> 21) & 511];   /* the fault stacks */
    m->pd[gi] = pd;
    m->pdpt[gi] = (uint64_t)(uintptr_t)pd | (m->pdpt[gi] & 0xFFFull) | PT_W;
    return pd;
}

/* [base, base + size) writable for the app (or read-only again): 0, -1 */
static int map(appmem_t* m, uintptr_t base, uint32_t size, int writable) {
    if (!m->pml4) return 0;
    for (uintptr_t a = base; a < base + size; a += 0x200000) {
        uint64_t* kpd = kernel_pd((int)(a >> 30));
        uint64_t* pd = app_pd(m, (int)(a >> 30));
        if (!pd || !kpd) return -1;
        uint64_t e = kpd[(a >> 21) & 511];
        pd[(a >> 21) & 511] = writable ? e | PT_W : e & ~PT_W;
    }
    return 0;
}
#else
static int map(appmem_t* m, uintptr_t base, uint32_t size, int writable) {
    (void)m; (void)base; (void)size; (void)writable;
    return 0;
}
#endif

void appmem_init(uintptr_t fault_base, uint32_t fault_size) {
    g_fault_base = fault_base;
    g_fault_size = fault_size;
#ifdef __x86_64__
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    g_kernel_cr3 = cr3 & ~(uintptr_t)0xFFF;
    appmem_cpu_init();
#endif
}

appmem_t* appmem_new(void) {
    appmem_t* m = (appmem_t*)kzalloc(sizeof(appmem_t));
    if (!m) return NULL;
#ifdef __x86_64__
    if (g_kernel_cr3 && build(m) != 0) { appmem_destroy(m); return NULL; }
#endif
    return m;
}

void appmem_destroy(appmem_t* m) {
    if (!m) return;
    for (chunk_t* c = m->chunks; c;) {
        chunk_t* n = c->next;
        kfree((void*)c->base);
        kfree(c);
        c = n;
    }
#ifdef __x86_64__
    for (int i = 0; i < 4; i++) if (m->pd[i]) kfree(m->pd[i]);
    if (m->pdpt) kfree(m->pdpt);
    if (m->pml4) kfree(m->pml4);
#endif
    kfree(m);
}

uintptr_t appmem_cr3(appmem_t* m) {
#ifdef __x86_64__
    return m && m->pml4 ? (uintptr_t)m->pml4 : 0;
#else
    (void)m;
    return 0;
#endif
}

void appmem_sync(appmem_t* m) {
    if (!m) return;
    for (chunk_t* c = m->chunks; c; c = c->next) map(m, c->base, c->size, 1);
}

/* a new chunk with room for need bytes: one free block over all of it */
static chunk_t* add_chunk(appmem_t* m, uint32_t need) {
    if (need > 0xE0000000u - AHDR - APPMEM_CHUNK) return NULL;
    uint32_t size = up(need + AHDR, APPMEM_CHUNK);
    chunk_t* c = (chunk_t*)kzalloc(sizeof(chunk_t));
    if (!c) return NULL;
    uint8_t* base = (uint8_t*)kmalloc_aligned(size, APPMEM_CHUNK);
    if (!base) { kfree(c); return NULL; }
    c->base = (uintptr_t)base;
    c->size = size;
    if (map(m, c->base, size, 1) != 0) {
        map(m, c->base, size, 0);
        kfree(base);
        kfree(c);
        return NULL;
    }
    ablk_t* b = (ablk_t*)base;
    b->size = size - AHDR;
    b->used = 0;
    b->prev = b->next = NULL;
    c->next = m->chunks;
    m->chunks = c;
    return c;
}

/* first fit in chunk c; NULL if none (or its blocks are not believable) */
static void* alloc_in(appmem_t* m, chunk_t* c, uint32_t need) {
    for (ablk_t* b = (ablk_t*)c->base; b; b = b->next) {
        if (!blk_ok(c, b)) {
            klog("appmem: the app's heap is damaged (chunk %p)\n", (void*)c->base);
            return NULL;
        }
        if (b->used || b->size < need) continue;
        if (b->size >= need + AHDR + AMIN) {            /* the rest: a free block */
            ablk_t* n = (ablk_t*)((uint8_t*)b + AHDR + need);
            n->size = b->size - need - AHDR;
            n->used = 0;
            n->prev = b;
            n->next = b->next;
            if (b->next) b->next->prev = n;
            b->next = n;
            b->size = need;
        }
        b->used = AMAGIC;
        m->used += b->size;
        return (uint8_t*)b + AHDR;
    }
    return NULL;
}

void* appmem_alloc(appmem_t* m, size_t n) {
    if (!m || !n || n > 0xC0000000u) return NULL;
    uint32_t need = up((uint32_t)n, 16);
    for (chunk_t* c = m->chunks; c; c = c->next) {
        if (c->size - AHDR < need) continue;
        void* p = alloc_in(m, c, need);
        if (p) return p;
    }
    chunk_t* c = add_chunk(m, need);
    return c ? alloc_in(m, c, need) : NULL;
}

/* the header of the block at p, if p is one of the app's blocks in use */
static ablk_t* used_blk(appmem_t* m, void* p, chunk_t** cp) {
    chunk_t* c = chunk_of(m, (uintptr_t)p);
    if (!c || (uintptr_t)p < c->base + AHDR) return NULL;
    ablk_t* b = (ablk_t*)((uint8_t*)p - AHDR);
    if (!blk_ok(c, b) || b->used != AMAGIC) return NULL;
    if (cp) *cp = c;
    return b;
}

/* aligned blocks: a little more is taken, and the block's own address is
 * kept in front of the aligned one (and checked when it is freed) */
#define ALIGNED_TAG 0x41A11C3Du

void* appmem_alloc_aligned(appmem_t* m, size_t n, uint32_t align) {
    if (!m || align > 4096 || (align & (align - 1))) return NULL;
    if (align <= 16) return appmem_alloc(m, n);
    uint8_t* raw = (uint8_t*)appmem_alloc(m, n + align + 16);
    if (!raw) return NULL;
    uintptr_t a = ((uintptr_t)raw + 16 + align - 1) & ~(uintptr_t)(align - 1);
    ((uintptr_t*)a)[-1] = (uintptr_t)raw;
    ((uint32_t*)a)[-3] = ALIGNED_TAG;
    return (void*)a;
}

void appmem_free(appmem_t* m, void* p) {
    if (!m || !p) return;
    chunk_t* c = NULL;
    ablk_t* b = used_blk(m, p, &c);
    if (!b && ((uintptr_t)p & 15) == 0 && chunk_of(m, (uintptr_t)p - 16) &&
        ((uint32_t*)p)[-3] == ALIGNED_TAG) {
        void* raw = (void*)((uintptr_t*)p)[-1];
        b = used_blk(m, raw, &c);
    }
    if (!b) return;                                     /* not a block of the app's: ignored */
    m->used = m->used >= b->size ? m->used - b->size : 0;
    b->used = 0;
    ablk_t* n = b->next;
    if (n && blk_ok(c, n) && !n->used) {                /* merge with the next one */
        b->size += AHDR + n->size;
        b->next = n->next;
        if (n->next) n->next->prev = b;
    }
    ablk_t* pv = b->prev;
    if (pv && blk_ok(c, pv) && pv->next == b && !pv->used) {   /* ... and the one before */
        pv->size += AHDR + b->size;
        pv->next = b->next;
        if (b->next) b->next->prev = pv;
    }
}

size_t appmem_size(appmem_t* m, void* p) {
    ablk_t* b = m ? used_blk(m, p, NULL) : NULL;
    return b ? b->size : 0;
}

int appmem_owns(appmem_t* m, const void* p, size_t n) {
    if (!m) return 0;
    chunk_t* c = chunk_of(m, (uintptr_t)p);
    return c && (uintptr_t)p + n <= c->base + c->size && (uintptr_t)p + n >= (uintptr_t)p;
}

uint32_t appmem_used(appmem_t* m) { return m ? m->used : 0; }

#ifdef __x86_64__
int appmem_tables_allow_write(uintptr_t cr3, uintptr_t addr) {
    if (!cr3 || cr3 == g_kernel_cr3 || addr >= (1ull << 47)) return 0;
    uint64_t e = ((uint64_t*)(uintptr_t)(cr3 & PT_ADDR))[(addr >> 39) & 511];
    if ((e & (PT_P | PT_W)) != (PT_P | PT_W)) return 0;
    e = tbl(e)[(addr >> 30) & 511];
    if ((e & (PT_P | PT_W)) != (PT_P | PT_W)) return 0;
    if (e & PT_LARGE) return 1;
    e = tbl(e)[(addr >> 21) & 511];
    if ((e & (PT_P | PT_W)) != (PT_P | PT_W)) return 0;
    if (e & PT_LARGE) return 1;
    e = tbl(e)[(addr >> 12) & 511];
    return (e & (PT_P | PT_W)) == (PT_P | PT_W);
}
#else
int appmem_tables_allow_write(uintptr_t cr3, uintptr_t addr) { (void)cr3; (void)addr; return 0; }
#endif

/* read-only pages bind the kernel's own code too (CR0.WP): without it the
 * processor lets ring-0 code - an app's - write them anyway. Every core. */
void appmem_cpu_init(void) {
#ifdef __x86_64__
    uintptr_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1u << 16;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
#endif
}
