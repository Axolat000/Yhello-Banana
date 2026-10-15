#include "umem.h"
#include "kheap.h"
#include "kstring.h"
#include "serial.h"

/*
 * Three sizes of allocation:
 *  - small (up to 2 KiB): slabs - a page (or 4) cut into equal pieces of
 *    one size class, a bitmap of the free ones;
 *  - up to 2 MiB: a run of whole 4 KiB pages inside one block;
 *  - bigger: blocks of their own, in a row. Freed, they become ordinary
 *    blocks the app's other allocations can use.
 * Every block, run and slab is described in kernel memory; g_frame maps a
 * 2 MiB frame of physical memory to the block that holds it (and to its
 * app: umem_owns checks a system call's pointers against it).
 */

#define BLOCK  0x200000u
#define PAGE   4096u
#define PAGES  (BLOCK / PAGE)           /* 512 */
#define FRAMES 2048                     /* 2 MiB frames: the first 4 GiB */
#define SMALL  2048u
#define LIMIT  (4ull << 30)                /* FRAMES * BLOCK, which a 32-bit uintptr_t cannot hold */

static const uint16_t g_cls[] = { 16, 32, 48, 64, 80, 96, 128, 160, 192, 256, 320, 384, 512, 768, 1024, 1536, 2048 };
#define NCLS ((int)(sizeof(g_cls) / sizeof(g_cls[0])))

typedef struct uslab {
    uintptr_t     base;
    uint16_t      cls, nobj, nfree, pages;
    uint64_t      map[4];               /* 1 = free (up to 256 pieces) */
    int           listed;               /* in its class's list of slabs with room */
    struct uslab* next;                 /* ... that list */
    struct uslab* all_next;             /* every slab of the app */
} uslab_t;

typedef struct uchunk {
    umem_t*        m;
    uintptr_t      base;
    void*          alloc;               /* what kfree takes (the first block of a row) */
    uint32_t       nframes;             /* > 1: one big allocation */
    int            big_used;
    int            broken;              /* its tables could not be made: never handed out */
    uint32_t       free_pages;
    uint64_t       used[PAGES / 64];    /* pages in use */
    uint16_t       run[PAGES];          /* pages of the run starting at this page (0: none) */
    uslab_t*       slab[PAGES];         /* the slab a page belongs to */
    struct uchunk* next;
} uchunk_t;

struct umem {
    uspace_t* us;
    uchunk_t* chunks;
    uslab_t*  partial[NCLS];
    uslab_t*  slabs;
    uint32_t  used;
};

static uchunk_t* g_frame[FRAMES];

umem_t* umem_create(uspace_t* us) {
    umem_t* m = (umem_t*)kzalloc(sizeof(umem_t));
    if (m) m->us = us;
    return m;
}

static uchunk_t* chunk_of(umem_t* m, const void* p) {
    uintptr_t a = (uintptr_t)p;
    if (a / BLOCK >= FRAMES) return NULL;
    uchunk_t* c = g_frame[a / BLOCK];
    return c && c->m == m ? c : NULL;
}

static void link_chunk(umem_t* m, uchunk_t* c) {
    for (uint32_t k = 0; k < c->nframes; k++) g_frame[c->base / BLOCK + k] = c;
    c->next = m->chunks;
    m->chunks = c;
}

/* n blocks in a row, zeroed and opened to the app */
static uchunk_t* new_chunk(umem_t* m, uint32_t n) {
    uint8_t* mem = (uint8_t*)kmalloc_aligned((size_t)n * BLOCK, BLOCK);
    if (!mem) return NULL;
    if ((uint64_t)(uintptr_t)mem + (uint64_t)n * BLOCK > LIMIT) { kfree(mem); return NULL; }
    uchunk_t* c = (uchunk_t*)kzalloc(sizeof(uchunk_t));
    if (!c) { kfree(mem); return NULL; }
    memset(mem, 0, (size_t)n * BLOCK);        /* nothing of the kernel's left in it */
    c->m = m;
    c->base = (uintptr_t)mem;
    c->alloc = mem;
    c->nframes = n;
    c->free_pages = n == 1 ? PAGES : 0;
    link_chunk(m, c);
    /* (if the tables cannot be made, the block is still the app's - given
     * back when it exits - but never handed out) */
    if (uspace_map(m->us, (uintptr_t)mem, n * BLOCK) < 0) {
        c->free_pages = 0;
        c->broken = 1;
        return NULL;
    }
    return c;
}

static int page_used(const uchunk_t* c, uint32_t i) { return (int)((c->used[i >> 6] >> (i & 63)) & 1); }

static void mark(uchunk_t* c, uint32_t i, uint32_t n, int used) {
    for (uint32_t k = i; k < i + n; k++) {
        if (used) c->used[k >> 6] |= 1ull << (k & 63);
        else c->used[k >> 6] &= ~(1ull << (k & 63));
    }
}

static int find_run(const uchunk_t* c, uint32_t n) {
    uint32_t len = 0;
    for (uint32_t i = 0; i < PAGES; i++) {
        if (page_used(c, i)) { len = 0; continue; }
        if (++len == n) return (int)(i + 1 - n);
    }
    return -1;
}

static void* take_run(uchunk_t* c, uint32_t i, uint32_t n) {
    mark(c, i, n, 1);
    c->run[i] = (uint16_t)n;
    c->free_pages -= n;
    return (void*)(c->base + (uintptr_t)i * PAGE);
}

/* a run's pages back to its block (no accounting) */
static void run_free(uchunk_t* c, uint32_t pi) {
    uint32_t n = c->run[pi];
    mark(c, pi, n, 0);
    c->run[pi] = 0;
    c->free_pages += n;
}

/* n (<= 512) pages in a row inside one block */
static void* pages_alloc(umem_t* m, uint32_t n) {
    for (uchunk_t* c = m->chunks; c; c = c->next) {
        if (c->nframes != 1 || c->free_pages < n) continue;
        int i = find_run(c, n);
        if (i >= 0) return take_run(c, (uint32_t)i, n);
    }
    uchunk_t* c = new_chunk(m, 1);
    return c ? take_run(c, 0, n) : NULL;
}

/* a freed big allocation: its blocks become ordinary ones */
static void split_big(umem_t* m, uchunk_t* big) {
    for (uchunk_t** pp = &m->chunks; *pp; pp = &(*pp)->next)
        if (*pp == big) { *pp = big->next; break; }
    for (uint32_t k = 0; k < big->nframes; k++) {
        uchunk_t* c = (uchunk_t*)kzalloc(sizeof(uchunk_t));
        if (!c) {
            /* (no memory for the bookkeeping: the rest stays one used block) */
            big->base += (uintptr_t)k * BLOCK;
            big->alloc = k ? NULL : big->alloc;
            big->nframes -= k;
            big->big_used = 1;
            link_chunk(m, big);
            return;
        }
        c->m = m;
        c->base = big->base + (uintptr_t)k * BLOCK;
        c->alloc = k ? NULL : big->alloc;
        c->nframes = 1;
        c->free_pages = PAGES;
        link_chunk(m, c);
    }
    kfree(big);
}

static int cls_of(size_t size) {
    for (int i = 0; i < NCLS; i++) if (size <= g_cls[i]) return i;
    return -1;
}

static void slab_unlist(umem_t* m, uslab_t* s) {
    if (!s->listed) return;
    for (uslab_t** pp = &m->partial[s->cls]; *pp; pp = &(*pp)->next)
        if (*pp == s) { *pp = s->next; break; }
    s->listed = 0;
}

static void* small_alloc(umem_t* m, int ci) {
    uslab_t* s = m->partial[ci];
    if (!s) {
        uint16_t pages = g_cls[ci] <= 256 ? 1 : 4;
        uint8_t* base = (uint8_t*)pages_alloc(m, pages);
        if (!base) return NULL;
        s = (uslab_t*)kzalloc(sizeof(uslab_t));
        if (!s) { uchunk_t* c = chunk_of(m, base); run_free(c, (uint32_t)(((uintptr_t)base - c->base) / PAGE)); return NULL; }
        s->base = (uintptr_t)base;
        s->cls = (uint16_t)ci;
        s->pages = pages;
        s->nobj = (uint16_t)(pages * PAGE / g_cls[ci]);
        s->nfree = s->nobj;
        for (uint32_t k = 0; k < s->nobj; k++) s->map[k >> 6] |= 1ull << (k & 63);
        uchunk_t* c = chunk_of(m, base);
        uint32_t pi = (uint32_t)((s->base - c->base) / PAGE);
        for (uint32_t k = 0; k < pages; k++) c->slab[pi + k] = s;
        s->all_next = m->slabs;
        m->slabs = s;
        s->next = NULL;
        s->listed = 1;
        m->partial[ci] = s;
    }
    for (int w = 0; w < 4; w++) {
        if (!s->map[w]) continue;
        int b = __builtin_ctzll(s->map[w]);
        s->map[w] &= ~(1ull << b);
        if (--s->nfree == 0) slab_unlist(m, s);
        m->used += g_cls[ci];
        return (void*)(s->base + (uintptr_t)(w * 64 + b) * g_cls[ci]);
    }
    return NULL;                                /* (cannot happen: a listed slab has room) */
}

void* umem_alloc(umem_t* m, size_t size) {
    if (!m || size > (1ul << 30)) return NULL;
    if (!size) size = 1;
    if (size <= SMALL) return small_alloc(m, cls_of(size));
    if (size <= BLOCK) {
        uint32_t n = (uint32_t)((size + PAGE - 1) / PAGE);
        void* p = pages_alloc(m, n);
        if (p) m->used += n * PAGE;
        return p;
    }
    uint32_t n = (uint32_t)((size + BLOCK - 1) / BLOCK);
    uchunk_t* c = new_chunk(m, n);
    if (!c) return NULL;
    c->big_used = 1;
    m->used += n * BLOCK;
    return (void*)c->base;
}

void* umem_alloc_pages(umem_t* m, size_t size, size_t guard) {
    if (!m || (guard & (PAGE - 1))) return NULL;
    size_t total = (size + guard + PAGE - 1) & ~(size_t)(PAGE - 1);
    uint8_t* p = (uint8_t*)umem_alloc(m, total < SMALL + 1 ? SMALL + 1 : total);   /* (whole pages) */
    if (!p) return NULL;
    if (guard) uspace_guard(m->us, (uintptr_t)p, (uint32_t)guard, 1);
    return p + guard;
}

void umem_free_pages(umem_t* m, void* p, size_t guard) {
    if (!m || !p) return;
    uint8_t* base = (uint8_t*)p - guard;
    if (guard) uspace_guard(m->us, (uintptr_t)base, (uint32_t)guard, 0);
    umem_free(m, base);
}

size_t umem_size(umem_t* m, const void* p) {
    uchunk_t* c = chunk_of(m, p);
    if (!c) return 0;
    uintptr_t a = (uintptr_t)p;
    if (c->broken) return 0;
    if (c->nframes > 1) return a == c->base && c->big_used ? (size_t)c->nframes * BLOCK : 0;
    uint32_t pi = (uint32_t)((a - c->base) / PAGE);
    uslab_t* s = c->slab[pi];
    if (s) {
        uintptr_t off = a - s->base;
        if (off % g_cls[s->cls]) return 0;
        uint32_t k = (uint32_t)(off / g_cls[s->cls]);
        if (k >= s->nobj || ((s->map[k >> 6] >> (k & 63)) & 1)) return 0;   /* free */
        return g_cls[s->cls];
    }
    if ((a & (PAGE - 1)) || !c->run[pi]) return 0;
    return (size_t)c->run[pi] * PAGE;
}

void umem_free(umem_t* m, void* p) {
    if (!m || !p) return;
    uchunk_t* c = chunk_of(m, p);
    uintptr_t a = (uintptr_t)p;
    if (!c) { klog("app: free() of a pointer that is not its memory (%p)\n", p); return; }
    if (c->broken) return;
    if (c->nframes > 1) {
        if (a != c->base || !c->big_used) { klog("app: free() of a bad pointer (%p)\n", p); return; }
        m->used -= c->nframes * BLOCK;
        c->big_used = 0;
        split_big(m, c);
        return;
    }
    uint32_t pi = (uint32_t)((a - c->base) / PAGE);
    uslab_t* s = c->slab[pi];
    if (s) {
        uintptr_t off = a - s->base;
        uint32_t k = (uint32_t)(off / g_cls[s->cls]);
        if (off % g_cls[s->cls] || k >= s->nobj || ((s->map[k >> 6] >> (k & 63)) & 1)) {
            klog("app: free() of a bad pointer (%p)\n", p);
            return;
        }
        s->map[k >> 6] |= 1ull << (k & 63);
        s->nfree++;
        m->used -= g_cls[s->cls];
        if (s->nfree == s->nobj && (m->partial[s->cls] != s || s->next)) {
            /* empty, and its class has another slab with room: the pages go back */
            slab_unlist(m, s);
            for (uslab_t** pp = &m->slabs; *pp; pp = &(*pp)->all_next)
                if (*pp == s) { *pp = s->all_next; break; }
            uint32_t first = (uint32_t)((s->base - c->base) / PAGE);
            for (uint32_t i = 0; i < s->pages; i++) c->slab[first + i] = NULL;
            run_free(c, first);
            kfree(s);
        } else if (!s->listed) {
            s->listed = 1;
            s->next = m->partial[s->cls];
            m->partial[s->cls] = s;
        }
        return;
    }
    if ((a & (PAGE - 1)) || !c->run[pi]) { klog("app: free() of a bad pointer (%p)\n", p); return; }
    m->used -= c->run[pi] * PAGE;
    run_free(c, pi);
}

void* umem_realloc(umem_t* m, void* p, size_t size) {
    if (!p) return umem_alloc(m, size);
    if (!size) { umem_free(m, p); return NULL; }
    size_t old = umem_size(m, p);
    if (!old) return NULL;
    if (size <= old && (old <= SMALL || size > old / 2)) return p;
    void* n = umem_alloc(m, size);
    if (!n) return NULL;
    memcpy(n, p, old < size ? old : size);
    umem_free(m, p);
    return n;
}

uint32_t umem_used(umem_t* m) { return m ? m->used : 0; }

int umem_owns(umem_t* m, const void* p, size_t n) {
    uintptr_t a = (uintptr_t)p;
    if (!m || !n) n = 1;
    if (!m || (uint64_t)a + n > LIMIT) return 0;
    for (uintptr_t f = a / BLOCK; f <= (uintptr_t)(((uint64_t)a + n - 1) / BLOCK); f++)
        if (!g_frame[f] || g_frame[f]->m != m) return 0;
    return 1;
}

int umem_owns_str(umem_t* m, const char* s) {
    uintptr_t a = (uintptr_t)s;
    for (;;) {
        if (!m || a / BLOCK >= FRAMES) return 0;
        uintptr_t f = a / BLOCK;
        if (!g_frame[f] || g_frame[f]->m != m) return 0;
        uint64_t end = (uint64_t)(f + 1) * BLOCK;
        for (; (uint64_t)a < end; a++) {
            if (!*(const volatile char*)a) return 1;
            if (a == (uintptr_t)-1) return 0;
        }
    }
}

void umem_destroy(umem_t* m) {
    if (!m) return;
    while (m->slabs) {
        uslab_t* s = m->slabs;
        m->slabs = s->all_next;
        kfree(s);
    }
    /* (the first block of a row holds the allocation of the whole row) */
    while (m->chunks) {
        uchunk_t* c = m->chunks;
        m->chunks = c->next;
        for (uint32_t k = 0; k < c->nframes; k++) g_frame[c->base / BLOCK + k] = NULL;
        kfree(c->alloc);
        kfree(c);
    }
    kfree(m);
}
