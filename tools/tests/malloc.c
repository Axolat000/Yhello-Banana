/* SPDX-License-Identifier: GPL-3.0-or-later */
/* tools/test_malloc.py: sdk/lib/malloc.c (built as the SDK builds it, its
 * functions renamed b_*) on Linux, under a stand-in for Banana OS's call
 * table: vm_map is mmap. Checks every byte of random blocks, realloc,
 * calloc, alignment, blocks freed by other threads, thread caches given
 * back when threads end, kernel memory passed back to the kernel - and
 * that nothing is left in use at the end. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "banana_api.h"

void* b_malloc(size_t n);
void  b_free(void* p);
void* b_realloc(void* p, size_t n);
void* b_calloc(size_t n, size_t s);
size_t b_malloc_usable_size(void* p);
void* __malloc_aligned(size_t align, size_t n);
int   __malloc_heap(void);
void  __malloc_stats(size_t* mapped, size_t* inuse);
void  __malloc_thread_end(void* cache);
extern void** (*__malloc_slot_fn)(void);

/* ── the stand-in system ── */
static banana_api_t g_api;
const banana_api_t* __banana = &g_api;
static volatile int g_kernel_frees;

static void* vm_map(void* addr, unsigned long len, int prot, int flags) {
    (void)prot;
    void* p = mmap(addr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE |
                   ((flags & BANANA_MAP_FIXED) ? MAP_FIXED : 0), -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
static int vm_unmap(void* addr, unsigned long len) { return munmap(addr, len); }
static void* k_malloc(unsigned long n) { return malloc(n); }       /* (glibc's heap: below BANANA_VM_BASE) */
static void k_free(void* p) { __sync_fetch_and_add(&g_kernel_frees, 1); free(p); }
static void* k_realloc(void* p, unsigned long n) { return realloc(p, n); }

void banana_lock(volatile int* m) { while (__sync_lock_test_and_set(m, 1)) while (*m) __builtin_ia32_pause(); }
void banana_unlock(volatile int* m) { __sync_lock_release(m); }
int __aligned_block(void* p, void** raw, size_t* size, int remove) { (void)p; (void)raw; (void)size; (void)remove; return 0; }

static __thread void* t_cache;
static void** slot(void) { return &t_cache; }

/* ── checks ── */
static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rnd(uint32_t* s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

static void fill(uint8_t* p, size_t n, uint32_t tag) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(tag + i * 7); }
static int good(const uint8_t* p, size_t n, uint32_t tag) { for (size_t i = 0; i < n; i++) if (p[i] != (uint8_t)(tag + i * 7)) return 0; return 1; }

static size_t pick(uint32_t* s) {
    uint32_t r = rnd(s);
    switch (r % 16) {
    case 0: return r % 300000;                  /* large */
    case 1: case 2: return r % 33000;           /* up to the biggest class */
    default: return r % 600;                    /* small, the common case */
    }
}

#define SLOTS 512
typedef struct { uint8_t* p; size_t n; uint32_t tag; } blk_t;

/* random malloc / realloc / free over SLOTS live blocks, every byte checked */
static void churn(uint32_t seed, int ops, blk_t* b) {
    uint32_t s = seed;
    for (int i = 0; i < ops; i++) {
        blk_t* x = &b[rnd(&s) % SLOTS];
        if (x->p) CHECK(good(x->p, x->n, x->tag), "block of %zu bytes changed", x->n);
        int what = rnd(&s) % 4;
        if (x->p && what == 0) {                /* realloc: the old part must survive */
            size_t n = pick(&s);
            uint8_t* q = b_realloc(x->p, n ? n : 1);
            CHECK(q != NULL, "realloc to %zu failed", n);
            if (!q) continue;
            size_t keep = x->n < n ? x->n : n;
            CHECK(good(q, keep, x->tag), "realloc %zu -> %zu lost data", x->n, n);
            x->p = q; x->n = n; x->tag = rnd(&s);
            fill(x->p, x->n, x->tag);
        } else if (x->p) {
            b_free(x->p);
            x->p = NULL;
        } else {
            size_t n = pick(&s);
            x->p = what == 1 ? b_calloc(1, n) : b_malloc(n);
            CHECK(x->p != NULL, "malloc %zu failed", n);
            if (!x->p) continue;
            if (what == 1) for (size_t k = 0; k < n; k++) if (x->p[k]) { CHECK(0, "calloc %zu not zeroed", n); break; }
            CHECK(((uintptr_t)x->p & 15) == 0, "malloc %zu not 16-byte aligned", n);
            CHECK(b_malloc_usable_size(x->p) >= n, "usable size %zu < %zu", b_malloc_usable_size(x->p), n);
            x->n = n; x->tag = rnd(&s);
            fill(x->p, x->n, x->tag);
        }
    }
}

static void drop(blk_t* b) { for (int i = 0; i < SLOTS; i++) if (b[i].p) { b_free(b[i].p); b[i].p = NULL; } }

/* blocks handed between threads: one frees what another allocated */
#define RING 4096
static void* volatile g_ring[RING];

static void* worker(void* arg) {
    uint32_t seed = (uint32_t)(uintptr_t)arg * 2654435761u + 1;
    blk_t* b = calloc(SLOTS, sizeof(blk_t));
    churn(seed, 300000, b);
    uint32_t s = seed;
    for (int i = 0; i < 100000; i++) {          /* swap blocks through the ring */
        void* mine = b_malloc(pick(&s) + 1);
        void* other = __sync_lock_test_and_set(&g_ring[rnd(&s) % RING], mine);
        if (other) b_free(other);
    }
    drop(b);
    free(b);
    __malloc_thread_end(t_cache);               /* (what pthread.c does when a thread ends) */
    t_cache = NULL;
    return NULL;
}

int main(void) {
    g_api.magic = BANANA_API_MAGIC;
    g_api.version = BANANA_API_VERSION;
    g_api.size = sizeof(g_api);
    g_api.vm_map = vm_map;
    g_api.vm_unmap = vm_unmap;
    g_api.malloc = k_malloc;
    g_api.free = k_free;
    g_api.realloc = k_realloc;

    CHECK(__malloc_heap(), "the heap is not used");

    /* one thread, no thread cache (every call takes the lock) */
    blk_t* b = calloc(SLOTS, sizeof(blk_t));
    churn(1, 400000, b);
    drop(b);
    printf("ok    one thread, no cache: 400000 random malloc / calloc / realloc / free, every byte checked\n");

    /* with a thread cache */
    __malloc_slot_fn = slot;
    churn(2, 400000, b);
    drop(b);
    printf("ok    one thread, with its cache\n");

    /* alignment: every power of two to 64 KiB, small and large */
    for (size_t a = 16; a <= 65536; a *= 2)
        for (size_t n = 1; n < 200000; n = n * 3 + 7) {
            uint8_t* p = __malloc_aligned(a, n);
            CHECK(p && ((uintptr_t)p % a) == 0, "aligned %zu for %zu: %p", a, n, (void*)p);
            if (!p) continue;
            fill(p, n, (uint32_t)a);
            CHECK(good(p, n, (uint32_t)a), "aligned block changed");
            b_free(p);
        }
    printf("ok    aligned blocks (16 B .. 64 KiB alignment, 1 B .. 200 KB)\n");

    /* the kernel's memory goes back to the kernel */
    void* k = g_api.malloc(100);
    int before = g_kernel_frees;
    b_free(k);
    CHECK(g_kernel_frees == before + 1, "free() of kernel memory did not reach the kernel");
    k = g_api.malloc(100);
    k = b_realloc(k, 5000);
    CHECK(k != NULL, "realloc of kernel memory");
    b_free(k);
    printf("ok    kernel memory: free and realloc pass it back\n");

    /* threads: their own churn, blocks freed by other threads */
    enum { T = 8 };
    pthread_t th[T];
    for (int i = 0; i < T; i++) pthread_create(&th[i], NULL, worker, (void*)(uintptr_t)(i + 10));
    for (int i = 0; i < T; i++) pthread_join(th[i], NULL);
    for (int i = 0; i < RING; i++) if (g_ring[i]) { b_free(g_ring[i]); g_ring[i] = NULL; }
    printf("ok    %d threads: 300000 random ops each, 100000 blocks freed by another thread\n", T);

    /* nothing may be left in use (the main thread's cache given back first) */
    __malloc_thread_end(t_cache);
    t_cache = NULL;
    size_t mapped, inuse;
    __malloc_stats(&mapped, &inuse);
    CHECK(inuse == 0, "%zu bytes still counted in use after everything was freed", inuse);
    printf("%s    everything freed: %zu bytes in use, %zu MiB of addresses reserved\n",
           inuse ? "FAIL" : "ok", inuse, mapped >> 20);

    printf("malloc: %s (%d failure%s)\n", g_fail ? "FAILED" : "all checks passed", g_fail, g_fail == 1 ? "" : "s");
    free(b);
    return g_fail ? 1 : 0;
}
