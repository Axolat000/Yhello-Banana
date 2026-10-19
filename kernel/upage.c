/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "upage.h"
#include "kheap.h"
#include "hmem.h"
#include "kstring.h"
#include "spinlock.h"

#define CHUNK   (2u * 1024u * 1024u)
#define PER     (CHUNK / 4096u)          /* 512 pages */

typedef struct pchunk {
    uintptr_t      base;
    uint32_t       npages;               /* 512 (2 MiB), or fewer when the heap had no 2 MiB in one piece */
    uint64_t       used[PER / 64];
    uint32_t       nused;
    int            high;                 /* from hmem (else the kernel heap) */
    struct pchunk* next;
} pchunk_t;

static pchunk_t*  g_chunks;              /* those with a free page first, roughly */
static uint64_t   g_used, g_held;
static spinlock_t g_lock = SPINLOCK_INIT; /* (apps fault pages in on every core) */

static pchunk_t* new_chunk(void) {
    pchunk_t* c = (pchunk_t*)kzalloc(sizeof(pchunk_t));
    if (!c) return NULL;
    uintptr_t mem = 0;
    c->npages = PER;
#ifdef __x86_64__
    mem = (uintptr_t)hmem_alloc(1);
    c->high = mem != 0;
#endif
    /* the heap: 2 MiB - or, when it is in pieces (compressed pages in it), less;
     * never its last 1/32 (4 MiB at least): what compressing apps' pages needs
     * (kernel/zswap.c: its entries, the compressed data), or nothing could
     * ever be freed again */
    static const uint32_t sizes[] = { PER, 64, 16 };
    uint32_t total = kheap_total_bytes(), used = kheap_used_bytes();
    uint32_t keep = total / 32u > (4u << 20) ? total / 32u : (4u << 20);
    uint32_t avail = total - used > keep ? total - used - keep : 0;
    for (int k = 0; !mem && k < 3; k++) {
        if ((size_t)sizes[k] * 4096u > avail) continue;
        mem = (uintptr_t)kmalloc_aligned((size_t)sizes[k] * 4096u, 4096);
        c->npages = sizes[k];
    }
    if (!mem) { kfree(c); return NULL; }
    c->base = mem;
    /* (the pages past npages: marked used, never handed out) */
    for (uint32_t i = c->npages; i < PER; i++) c->used[i / 64] |= 1ull << (i % 64);
    return c;
}

static void free_chunk(pchunk_t* c) {
#ifdef __x86_64__
    if (c->high) hmem_free(c->base, 1);
    else
#endif
        kfree((void*)c->base);
    kfree(c);
}

#define RESERVE 16u                              /* pages: see upage_alloc_reserve */

static void* alloc(int reserve) {
    uintptr_t f = spin_lock_irq(&g_lock);
    pchunk_t* c = g_chunks;
    while (c && c->nused == c->npages) c = c->next;
    if (!c || (!reserve && g_held - g_used <= RESERVE * 4096u)) {
        spin_unlock_irq(&g_lock, f);
        pchunk_t* n = new_chunk();                   /* (the heap takes its own lock) */
        f = spin_lock_irq(&g_lock);
        if (n) {
            n->next = g_chunks;
            g_chunks = n;
            g_held += (uint64_t)n->npages * 4096u;
            c = n;
        } else if (!c || !reserve) {
            spin_unlock_irq(&g_lock, f);
            return NULL;
        }
    }
    for (uint32_t w = 0; w < PER / 64; w++) {
        if (c->used[w] == ~0ull) continue;
        uint32_t b = (uint32_t)__builtin_ctzll(~c->used[w]);
        c->used[w] |= 1ull << b;
        c->nused++;
        g_used += 4096;
        void* p = (void*)(c->base + (uintptr_t)(w * 64 + b) * 4096u);
        spin_unlock_irq(&g_lock, f);
        return p;
    }
    spin_unlock_irq(&g_lock, f);
    return NULL;                                     /* (not reached: nused < PER) */
}

void* upage_alloc(void) { return alloc(0); }
void* upage_alloc_reserve(void) { return alloc(1); }

void upage_free(void* p) {
    uintptr_t a = (uintptr_t)p;
    pchunk_t* drop = NULL;
    uintptr_t f = spin_lock_irq(&g_lock);
    for (pchunk_t** pp = &g_chunks; *pp; pp = &(*pp)->next) {
        pchunk_t* c = *pp;
        if (a < c->base || a >= c->base + (uintptr_t)c->npages * 4096u) continue;
        uint32_t i = (uint32_t)((a - c->base) / 4096u);
        if (!(c->used[i / 64] & (1ull << (i % 64)))) break;      /* (not handed out: ignored) */
        c->used[i / 64] &= ~(1ull << (i % 64));
        c->nused--;
        g_used -= 4096;
        if (!c->nused) {                             /* empty: back to the system */
            *pp = c->next;
            g_held -= (uint64_t)c->npages * 4096u;
            drop = c;
        } else if (pp != &g_chunks) {                /* has room: to the front */
            *pp = c->next;
            c->next = g_chunks;
            g_chunks = c;
        }
        break;
    }
    spin_unlock_irq(&g_lock, f);
    if (drop) free_chunk(drop);
}

uint64_t upage_used_bytes(void) { return g_used; }
uint64_t upage_held_bytes(void) { return g_held; }
