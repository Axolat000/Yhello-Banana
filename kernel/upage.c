#include "upage.h"
#include "kheap.h"
#include "hmem.h"
#include "kstring.h"
#include "spinlock.h"

#define CHUNK   (2u * 1024u * 1024u)
#define PER     (CHUNK / 4096u)          /* 512 pages */

typedef struct pchunk {
    uintptr_t      base;
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
#ifdef __x86_64__
    mem = (uintptr_t)hmem_alloc(1);
    c->high = mem != 0;
#endif
    if (!mem) mem = (uintptr_t)kmalloc_aligned(CHUNK, 4096);
    if (!mem) { kfree(c); return NULL; }
    c->base = mem;
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

void* upage_alloc(void) {
    uintptr_t f = spin_lock_irq(&g_lock);
    pchunk_t* c = g_chunks;
    while (c && c->nused == PER) c = c->next;
    if (!c) {
        spin_unlock_irq(&g_lock, f);
        pchunk_t* n = new_chunk();                   /* (the heap takes its own lock) */
        if (!n) return NULL;
        f = spin_lock_irq(&g_lock);
        n->next = g_chunks;
        g_chunks = n;
        g_held += CHUNK;
        c = n;
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

void upage_free(void* p) {
    uintptr_t a = (uintptr_t)p;
    pchunk_t* drop = NULL;
    uintptr_t f = spin_lock_irq(&g_lock);
    for (pchunk_t** pp = &g_chunks; *pp; pp = &(*pp)->next) {
        pchunk_t* c = *pp;
        if (a < c->base || a >= c->base + CHUNK) continue;
        uint32_t i = (uint32_t)((a - c->base) / 4096u);
        if (!(c->used[i / 64] & (1ull << (i % 64)))) break;      /* (not handed out: ignored) */
        c->used[i / 64] &= ~(1ull << (i % 64));
        c->nused--;
        g_used -= 4096;
        if (!c->nused) {                             /* empty: back to the system */
            *pp = c->next;
            g_held -= CHUNK;
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
