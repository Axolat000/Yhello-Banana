/* tools/test_kheap.py: kernel/kheap.c on Linux. Its heap lies in memory
 * mapped below 4 GiB (it keeps addresses in 32 bits), handed to it through a
 * made-up Multiboot2 memory map. Random kmalloc / kfree / krealloc /
 * kmalloc_aligned with every byte checked and kheap_check() along the way;
 * everything freed must leave one free block. Then the same fragmented
 * workload on this heap and on the one before it (old_*: the previous
 * kheap.c from git), timed. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

void  kheap_init(uint32_t mb2);
void* kmalloc(size_t n);
void  kfree(void* p);
void* krealloc(void* p, size_t n);
void* kmalloc_aligned(size_t n, size_t align);
uint32_t kheap_used_bytes(void);
int   kheap_check(char* err, int cap);
uint32_t kheap_free_blocks(void);

void  old_kheap_init(uint32_t mb2);
void* old_kmalloc(size_t n);
void  old_kfree(void* p);

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rnd(uint32_t* s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

/* a Multiboot2 information block below 4 GiB whose memory map has one
 * stretch of RAM: [base, base + len) */
static uint32_t mb2_with(void* base, size_t len) {
    uint8_t* m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    uint32_t* w = (uint32_t*)m;
    w[0] = 8 + 16 + 24 + 8; w[1] = 0;                     /* total size, reserved */
    w[2] = 6; w[3] = 16 + 24; w[4] = 24; w[5] = 0;        /* the memory map tag */
    uint64_t* e = (uint64_t*)(m + 24);
    e[0] = (uint64_t)(uintptr_t)base; e[1] = len;
    ((uint32_t*)(m + 40))[0] = 1; ((uint32_t*)(m + 40))[1] = 0;  /* available */
    ((uint32_t*)(m + 48))[0] = 0; ((uint32_t*)(m + 48))[1] = 8;  /* the end tag */
    return (uint32_t)(uintptr_t)m;
}

static void* region(size_t len) {
    void* p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) { perror("mmap below 4 GiB"); exit(2); }
    return p;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

#define LIVE 4000
typedef struct { uint8_t* p; size_t n; uint8_t tag; } blk_t;

static size_t pick(uint32_t* s) {
    uint32_t r = rnd(s);
    return r % 8 == 0 ? r % 200000 + 1 : r % 8 == 1 ? r % 20000 + 1 : r % 700 + 1;
}

static void check_heap(const char* when) {
    char err[160] = "";
    int bad = kheap_check(err, sizeof(err));
    CHECK(!bad, "heap check %s: %s", when, err);
}

/* the same workload for both heaps: LIVE long-lived blocks, every other
 * one freed (holes), then malloc / free pairs */
static double bench(void* (*m)(size_t), void (*f)(void*), int pairs) {
    static void* live[20000];
    uint32_t s = 99;
    for (int i = 0; i < 20000; i++) live[i] = m(rnd(&s) % 2000 + 16);
    for (int i = 0; i < 20000; i += 2) { f(live[i]); live[i] = NULL; }
    double t0 = now();
    for (int i = 0; i < pairs; i++) {
        void* p = m(rnd(&s) % 4000 + 16);
        f(p);
    }
    double t = now() - t0;
    for (int i = 0; i < 20000; i++) if (live[i]) f(live[i]);
    return t;
}

int main(void) {
    size_t len = 256u << 20;
    kheap_init(mb2_with(region(len), len));

    /* random use, every byte checked */
    static blk_t b[LIVE];
    uint32_t s = 7;
    for (int i = 0; i < 300000; i++) {
        blk_t* x = &b[rnd(&s) % LIVE];
        if (x->p) for (size_t k = 0; k < x->n; k++) if (x->p[k] != (uint8_t)(x->tag + k)) { CHECK(0, "a block of %zu changed", x->n); break; }
        uint32_t what = rnd(&s) % 5;
        if (x->p && what == 0) {
            size_t n = pick(&s);
            uint8_t* q = krealloc(x->p, n);
            CHECK(q != NULL, "krealloc %zu", n);
            if (!q) continue;
            size_t keep = x->n < n ? x->n : n;
            for (size_t k = 0; k < keep; k++) if (q[k] != (uint8_t)(x->tag + k)) { CHECK(0, "krealloc lost data"); break; }
            x->p = q; x->n = n;
            for (size_t k = 0; k < n; k++) q[k] = (uint8_t)(x->tag + k);
        } else if (x->p) {
            kfree(x->p);
            x->p = NULL;
        } else {
            size_t n = pick(&s);
            size_t align = what == 1 ? (size_t)1 << (6 + rnd(&s) % 16) : 0;   /* 64 B .. 2 MiB */
            x->p = align ? kmalloc_aligned(n, align) : kmalloc(n);
            CHECK(x->p != NULL, "kmalloc %zu (align %zu)", n, align);
            if (!x->p) continue;
            CHECK(((uintptr_t)x->p & (align ? align - 1 : 15)) == 0, "block not aligned to %zu", align ? align : 16);
            x->n = n; x->tag = (uint8_t)rnd(&s);
            for (size_t k = 0; k < n; k++) x->p[k] = (uint8_t)(x->tag + k);
        }
        if (i % 5000 == 4999) check_heap("along the way");
    }
    printf("ok    300000 random kmalloc / kfree / krealloc / kmalloc_aligned (64 B - 2 MiB), every byte checked\n");
    for (int i = 0; i < LIVE; i++) if (b[i].p) { kfree(b[i].p); b[i].p = NULL; }
    check_heap("after freeing everything");
    CHECK(kheap_used_bytes() == 0, "%u bytes still counted used", kheap_used_bytes());
    CHECK(kheap_free_blocks() == 1, "%u free blocks left (want 1: everything merged)", kheap_free_blocks());
    printf("%s    everything freed: %u bytes used, %u free block(s)\n", kheap_used_bytes() || kheap_free_blocks() != 1 ? "FAIL" : "ok",
           kheap_used_bytes(), kheap_free_blocks());

    /* timed: this heap against the one before it */
    old_kheap_init(mb2_with(region(len), len));
    int pairs = 300000;
    double tn = bench(kmalloc, kfree, pairs), to = bench(old_kmalloc, old_kfree, pairs);
    check_heap("after the benchmark");
    printf("ok    %d malloc + free among 10000 live blocks: %.0f ns each now, %.0f ns before (%.1fx)\n",
           pairs, tn / pairs * 1e9, to / pairs * 1e9, to / tn);
    CHECK(tn < to, "the new heap is not faster");

    printf("kheap: %s (%d failure%s)\n", g_fail ? "FAILED" : "all checks passed", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
