/* tools/test_malloc.py: sdk/lib/malloc.c (built as the SDK builds it, its
 * functions renamed b_*) on Linux, under a stand-in for Banana OS's call
 * table: vm_map is mmap. Checks every byte of random blocks, realloc,
 * calloc, alignment, blocks freed by other threads, mapping failures,
 * kernel memory passed back to the kernel - and
 * that nothing is left in use at the end. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <stddef.h>
#include <sched.h>
#include "banana_api.h"

void* b_malloc(size_t n);
void  b_free(void* p);
void* b_realloc(void* p, size_t n);
void* b_calloc(size_t n, size_t s);
size_t b_malloc_usable_size(void* p);
void* __malloc_aligned(size_t align, size_t n);
int   __malloc_heap(void);
void  __malloc_stats(size_t* mapped, size_t* inuse);
int b_posix_memalign(void** p, size_t alignment, size_t size);

/* ── the stand-in system ── */
static banana_api_t g_api;
const banana_api_t* __banana = &g_api;
static volatile int g_kernel_frees;

static size_t map_calls, unmap_calls, mapped_bytes;
static int fail_map, fail_unmap;
static void* vm_map(void* addr, unsigned long len, int prot, int flags) {
    if (addr || flags || prot != (BANANA_PROT_READ | BANANA_PROT_WRITE)) abort();
    map_calls++;
    if (fail_map) return NULL;
    void* p = mmap((void*)BANANA_VM_BASE, len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return NULL;
    if ((uintptr_t)p < BANANA_VM_BASE) abort();
    mapped_bytes += len;
    return p;
}
static int vm_unmap(void* addr, unsigned long len) {
    unmap_calls++;
    if (fail_unmap) return -1;
    int rc = munmap(addr, len);
    if (!rc) mapped_bytes -= len;
    return rc;
}
static void k_yield(void) { sched_yield(); }
static void* k_malloc(unsigned long n) { return malloc(n); }       /* (glibc's heap: below BANANA_VM_BASE) */
static void k_free(void* p) { __sync_fetch_and_add(&g_kernel_frees, 1); free(p); }
static void* k_realloc(void* p, unsigned long n) { return realloc(p, n); }

void banana_lock(volatile int* m) { while (__sync_lock_test_and_set(m, 1)) while (*m) __builtin_ia32_pause(); }
void banana_unlock(volatile int* m) { __sync_lock_release(m); }
/* ── checks ── */
static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { __sync_fetch_and_add(&g_fail, 1); printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

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
    return NULL;
}

static pthread_barrier_t first_barrier;
static void* first_use(void* arg) {
    (void)arg;
    pthread_barrier_wait(&first_barrier);
    void* p = b_malloc(31);
    CHECK(p != NULL, "simultaneous first use");
    b_free(p);
    return NULL;
}

static void edge_cases(void) {
    void* p = b_malloc(0);
    CHECK(p != NULL, "malloc(0)");
    CHECK(b_realloc(p, 0) == NULL, "realloc(p,0)");
    b_free(NULL);
    CHECK(b_malloc(SIZE_MAX) == NULL, "malloc overflow");
    CHECK(b_calloc(SIZE_MAX, 2) == NULL, "calloc overflow");
    void* out = (void*)123;
    CHECK(b_posix_memalign(&out, 3, 17) == EINVAL && out == (void*)123, "invalid alignment preserves out");
    CHECK(b_posix_memalign(&out, (size_t)1 << 63, SIZE_MAX) == ENOMEM && out == (void*)123, "alignment overflow preserves out");
    for (size_t a = 1u << 20; a <= (1ul << 30); a *= 32) {
        CHECK(!b_posix_memalign(&out, a, 17), "very large alignment");
        if (out != (void*)123) {
            CHECK(!((uintptr_t)out & (a - 1)), "very large alignment address");
            memset(out, 0xa5, 17);
            b_free(out);
        }
    }
    unsigned char* live = b_malloc(1024);
    CHECK(live != NULL, "live allocation for failure test");
    if (!live) return;
    memset(live, 0x6b, 1024);
    size_t before, inuse;
    __malloc_stats(&before, &inuse);
    fail_map = 1;
    CHECK(b_malloc(8u << 20) == NULL, "injected map failure");
    CHECK(b_realloc(live, 8u << 20) == NULL, "failed realloc preserves pointer");
    fail_map = 0;
    size_t after, after_use;
    __malloc_stats(&after, &after_use);
    CHECK(before == after && inuse == after_use, "failed map changed accounting");
    void* large = b_malloc(2u << 20);
    CHECK(large != NULL, "large allocation for unmap failure");
    __malloc_stats(&before, NULL);
    size_t calls = unmap_calls;
    fail_unmap = 1;
    b_free(large);
    __malloc_stats(&after, NULL);
    CHECK(unmap_calls > calls && after == before, "failed unmap must retain accounting");
    fail_map = 1;
    large = b_malloc(2u << 20);
    CHECK(large != NULL, "failed unmap leaves a reusable arena");
    fail_map = fail_unmap = 0;
    b_free(large);
    for (int i = 0; i < 1024; i++) CHECK(live[i] == 0x6b, "failure corrupted live block");
    b_free(live);
    printf("ok    zero/overflow/huge alignment and VM failure injection\n");
}

int main(int argc, char** argv) {
    g_api.magic = BANANA_API_MAGIC;
    g_api.version = BANANA_API_VERSION;
    g_api.size = sizeof(g_api);
    g_api.vm_map = vm_map;
    g_api.vm_unmap = vm_unmap;
    g_api.malloc = k_malloc;
    g_api.free = k_free;
    g_api.realloc = k_realloc;

    g_api.yield = k_yield;
    if (argc > 1 && strcmp(argv[1], "heap")) {
        if (!strcmp(argv[1], "old-api")) g_api.version = 15;
        if (!strcmp(argv[1], "short-api")) g_api.size = offsetof(banana_api_t, vm_unmap);
        if (!strcmp(argv[1], "no-map")) g_api.vm_map = NULL;
        if (!strcmp(argv[1], "no-unmap")) g_api.vm_unmap = NULL;
        CHECK(!__malloc_heap(), "fallback capability detection");
        void* p = b_malloc(0);
        CHECK(p != NULL, "fallback malloc(0)");
        b_free(p);
        for (size_t a = 32; a <= 65536; a *= 2) {
            p = NULL;
            CHECK(!b_posix_memalign(&p, a, 257), "fallback aligned allocation");
            if (!p) continue;
            CHECK(!((uintptr_t)p & (a - 1)), "fallback alignment");
            memset(p, 0x5a, 257);
            void* q = b_realloc(p, 8193);
            CHECK(q && good(q, 0, 0), "fallback aligned realloc");
            if (q) { for (int i = 0; i < 257; i++) CHECK(((unsigned char*)q)[i] == 0x5a, "fallback preservation"); }
            b_free(q ? q : p);
        }
        CHECK(map_calls == 0, "fallback must not map");
        printf("fallback %s: %s\n", argv[1], g_fail ? "FAILED" : "passed");
        return !!g_fail;
    }

    /* First allocation races capability initialization across eight threads. */
    pthread_t first[8];
    pthread_barrier_init(&first_barrier, NULL, 8);
    for (int i = 0; i < 8; i++) pthread_create(&first[i], NULL, first_use, NULL);
    for (int i = 0; i < 8; i++) pthread_join(first[i], NULL);
    pthread_barrier_destroy(&first_barrier);
    CHECK(__malloc_heap(), "the heap is not used");
    edge_cases();

    /* one thread, no thread cache (every call takes the lock) */
    blk_t* b = calloc(SLOTS, sizeof(blk_t));
    churn(1, 400000, b);
    drop(b);
    printf("ok    one thread, no cache: 400000 random malloc / calloc / realloc / free, every byte checked\n");

    /* Repeat with another deterministic seed. */
    churn(2, 400000, b);
    drop(b);
    printf("ok    one thread, second seed\n");

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

    /* No thread caches: all live bytes must have been returned. */
    size_t mapped, inuse;
    __malloc_stats(&mapped, &inuse);
    CHECK(mapped == mapped_bytes, "mapped accounting %zu != %zu", mapped, mapped_bytes);
    CHECK(inuse == 0, "%zu bytes still counted in use after everything was freed", inuse);
    printf("%s    everything freed: %zu bytes in use, %zu MiB of addresses reserved\n",
           inuse ? "FAIL" : "ok", inuse, mapped >> 20);

    printf("malloc: %s (%d failure%s)\n", g_fail ? "FAILED" : "all checks passed", g_fail, g_fail == 1 ? "" : "s");
    free(b);
    return g_fail ? 1 : 0;
}
