/* systest - the app half of `selftest` (a Banana OS shell command): each
 * mode is one check, and the exit code is the result (0 = it worked).
 *
 *   systest ring           0 if the app runs in user mode (ring 3)
 *   systest mem N [high]   allocates N MiB in 4 MiB pieces, fills and checks
 *                          every byte; with "high", every piece must lie above
 *                          4 GiB (the RAM the 64-bit kernel gives apps first)
 *   systest threads N      N threads at once, each one's result checked
 *   systest files N        N files open at once, written and read back
 *   systest cores          a thread per core calling malloc / free and a lock
 *                          over and over; exit code 100 + the cores it saw
 *   systest idle MS        waits MS milliseconds (for many apps at once)
 *   systest swap N MS      maps N MiB, fills every page (zeros, text, noise),
 *                          waits MS ms (selftest compresses it meanwhile:
 *                          kernel/zswap.c) and checks every byte again
 *   systest tlb            a thread per core maps, marks, write-protects and
 *                          unmaps pages of its own over and over: a fresh
 *                          page must read zero; exit code 100 + the cores seen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <banana.h>
#include <sys/mman.h>
#include <stdint.h>
#include "banana_api.h"

static unsigned xs(unsigned* x) {
    *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5;
    return *x;
}

static int t_ring(void) {
#if defined(__x86_64__) || defined(__i386__)
    unsigned short cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    printf("code segment %#x: ring %d\n", cs, cs & 3);
    return (cs & 3) == 3 ? 0 : 1;
#else
    return 1;
#endif
}

#define PIECE (4u << 20)

/* Include ordinary page-sized blocks and aligned blocks: free/realloc must
 * never inspect bytes before an ordinary allocation's mapping. */
static int t_allocators(void) {
    static const size_t sizes[] = { 16, 32, 4096, 8192, 2u << 20 };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        unsigned char* p = malloc(sizes[i]);
        if (!p) return 1;
        memset(p, 0x5A, sizes[i]);
        unsigned char* q = realloc(p, sizes[i] + 4096);
        if (!q) { free(p); return 1; }
        for (size_t k = 0; k < sizes[i]; k++)
            if (q[k] != 0x5A) { free(q); return 1; }
        free(q);
    }
    for (size_t align = 32; align <= 8192; align *= 2) {
        void* p;
        if (posix_memalign(&p, align, 257) || ((unsigned long)p & (align - 1))) return 1;
        memset(p, 0xA5, 257);
        /* Failed realloc must leave the old allocation available to free. */
        void* failed = realloc(p, (size_t)-1 / 2);
        if (failed) { free(failed); return 1; }
        unsigned char* q = realloc(p, 8193);
        if (!q) { free(p); return 1; }
        for (int k = 0; k < 257; k++)
            if (q[k] != 0xA5) { free(q); return 1; }
        free(q);
        if (posix_memalign(&p, align, 257)) return 1;
        free(p);
    }
    printf("ordinary and aligned malloc/realloc/free: ok\n");
    return 0;
}

static int t_mem(int mib, int high) {
    if (t_allocators()) return 4;
    int n = mib / 4 ? mib / 4 : 1;
    unsigned char** p = calloc((size_t)n, sizeof(*p));
    if (!p) return 1;
    int rc = 0, low = 0;
    for (int i = 0; i < n && !rc; i++) {
        p[i] = malloc(PIECE);
        if (!p[i]) { printf("piece %d: out of memory\n", i); rc = 1; break; }
        if (!((unsigned long long)(unsigned long)p[i] >> 32)) low++;     /* below 4 GiB */
        unsigned x = 1234u + (unsigned)i;
        for (unsigned k = 0; k < PIECE; k++) p[i][k] = (unsigned char)xs(&x);
    }
    for (int i = 0; i < n && !rc; i++) {
        unsigned x = 1234u + (unsigned)i;
        for (unsigned k = 0; k < PIECE; k++)
            if (p[i][k] != (unsigned char)xs(&x)) { printf("piece %d: byte %u changed\n", i, k); rc = 2; break; }
    }
    printf("%d MiB in %d pieces, %d of them below 4 GiB\n", n * 4, n, low);
    for (int i = 0; i < n; i++) free(p[i]);
    free(p);
    printf("all %d pieces freed\n", n);
    if (!rc && high && low) rc = 3;
    return rc;
}

static int worker(void* arg) {
    long id = (long)arg;
    unsigned x = (unsigned)id * 7919u + 1, sum = 0;
    for (int i = 0; i < 20000; i++) sum += xs(&x) & 0xFF;
    return (int)(sum % 1000) + (int)id * 1000;
}

static int return_value(void* arg) { return (int)(long)arg; }

static int t_threads(int n) {
    static const int values[] = { 0, 1, -1, 0x7FFFFFFF, (-0x7FFFFFFF - 1) };
    int bad = 0;
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        int id = banana_thread(return_value, (void*)(long)values[i]);
        int got = id > 0 ? banana_join(id) : 0;
        if (id <= 0 || got != values[i]) {
            printf("return-value check: got %d, wanted %d (thread %d)\n", got, values[i], id);
            bad++;
        }
    }
    int* ids = calloc((size_t)n, sizeof(int));
    if (!ids) return 1;
    int started = 0;
    for (int i = 0; i < n; i++) {
        ids[i] = banana_thread(worker, (void*)(long)i);
        if (ids[i] > 0) started++;
    }
    for (int i = 0; i < n; i++) {
        if (ids[i] <= 0) continue;
        int got = banana_join(ids[i]);
        unsigned x = (unsigned)i * 7919u + 1, sum = 0;
        for (int k = 0; k < 20000; k++) sum += xs(&x) & 0xFF;
        int want = (int)(sum % 1000) + i * 1000;
        if (got != want) {
            if (bad < 4) printf("thread %d: returned %d, wanted %d\n", i, got, want);
            bad++;
        }
    }
    printf("%d of %d threads started, %d wrong results\n", started, n, bad);
    free(ids);
    return started == n && !bad ? 0 : 1;
}

static int t_files(int n) {
    FILE** f = calloc((size_t)n, sizeof(FILE*));
    if (!f) return 1;
    char name[64];
    int opened = 0, bad = 0;
    for (int i = 0; i < n; i++) {
        snprintf(name, sizeof(name), "/tmp/systest-%d.txt", i);
        f[i] = fopen(name, "w");
        if (f[i]) { opened++; fprintf(f[i], "file %d\n", i); }
    }
    for (int i = 0; i < n; i++) if (f[i]) fclose(f[i]);
    for (int i = 0; i < n; i++) {
        char want[32], got[32] = "";
        snprintf(name, sizeof(name), "/tmp/systest-%d.txt", i);
        snprintf(want, sizeof(want), "file %d\n", i);
        FILE* r = fopen(name, "r");
        if (!r || !fgets(got, sizeof(got), r) || strcmp(got, want)) bad++;
        if (r) fclose(r);
        remove(name);
    }
    printf("%d of %d files open at once, %d read back wrong\n", opened, n, bad);
    free(f);
    return opened == n && !bad ? 0 : 1;
}

/* ── cores ── */
static banana_mutex_t g_lock = BANANA_MUTEX_INIT;
static volatile int g_total;
static volatile unsigned long long g_seen;          /* bit n: a thread ran on core n (its APIC id) */

static int core_id(void) {
#if defined(__x86_64__) || defined(__i386__)
    unsigned a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return (int)(b >> 24);
#else
    return 0;
#endif
}

#define ROUNDS 20000

static int core_worker(void* arg) {
    (void)arg;
    int bad = 0;
    for (int i = 0; i < ROUNDS; i++) {
        size_t n = 16 + (size_t)(i % 64) * 24;
        unsigned char* b = malloc(n);
        if (!b) return 1;
        memset(b, i & 0xFF, n);
        if (b[0] != (i & 0xFF) || b[n - 1] != (i & 0xFF)) bad = 1;
        free(b);
        if (i % 1024 == 0) {
            void* aligned;
            if (posix_memalign(&aligned, 4096, 257)) return 1;
            memset(aligned, 0x5A, 257);
            unsigned char* grown = realloc(aligned, 8193);
            if (!grown) { free(aligned); return 1; }
            for (int k = 0; k < 257; k++) if (grown[k] != 0x5A) bad = 1;
            free(grown);
        }
        banana_lock(&g_lock);
        g_total++;
        banana_unlock(&g_lock);
        if (i % 1000 == 0) {
            int id = core_id();
            if (id < 64) __sync_fetch_and_or(&g_seen, 1ull << id);
        }
    }
    return bad;
}

static int t_cores(void) {
    int n = banana_cpus();
    if (n < 2) n = 2;
    if (n > 16) n = 16;
    int ids[16], bad = 0;
    unsigned t0 = banana_ticks();
    for (int i = 0; i < n; i++) ids[i] = banana_thread(core_worker, NULL);
    for (int i = 0; i < n; i++) {
        int got = ids[i] > 0 ? banana_join(ids[i]) : -1;
        if (got != 0) { printf("core worker %d: returned %d (want 0)\n", i, got); bad = 1; }
    }
    int cores = 0;
    for (int k = 0; k < 64; k++) if (g_seen & (1ull << k)) cores++;
    printf("%d threads x %d malloc/free + lock: total %d (want %d), on %d core%s, %u ms\n",
           n, ROUNDS, g_total, n * ROUNDS, cores, cores == 1 ? "" : "s", banana_ticks() - t0);
    if (bad || g_total != n * ROUNDS) return 1;
    return 100 + cores;
}

/* tlb: each thread has two pages at addresses of its own, mapped again
 * (MAP_FIXED, or after munmap) every round - fresh pages, which must read
 * zero - marked, made read-only and writable again; now and then it sleeps
 * (task switches, other cores: what their TLBs keep, kernel/tlb.c). A
 * fresh page showing the last round's mark was read through a translation
 * some core should have forgotten. */
#define TLB_ROUNDS 3000
static unsigned char* g_tlb_area;

static int tlb_worker(void* arg) {
    long id = (long)arg;
    unsigned char* p = g_tlb_area + id * 2 * 4096;
    for (int r = 0; r < TLB_ROUNDS; r++) {
        if (r & 1) munmap(p, 2 * 4096);
        if (mmap(p, 2 * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != p) return 2;
        if (p[0] || p[4096] || p[4095]) {
            printf("thread %ld, round %d: a fresh page reads %u / %u (the last round's mark)\n", id, r, p[0], p[4096]);
            return 3;
        }
        unsigned char mark = (unsigned char)(r % 255 + 1);
        p[0] = p[4096] = mark;
        if (mprotect(p, 2 * 4096, PROT_READ) || p[0] != mark) return 4;
        if (mprotect(p, 2 * 4096, PROT_READ | PROT_WRITE)) return 4;
        p[4095] = mark;
        if (r % 50 == 0) {
            int c = core_id();
            if (c < 64) __sync_fetch_and_or(&g_seen, 1ull << c);
            banana_sleep(1);
        }
    }
    return 0;
}

static int t_tlb(void) {
    int n = banana_cpus();
    if (n < 2) n = 2;
    if (n > 16) n = 16;
    g_tlb_area = mmap(NULL, (size_t)n * 2 * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_tlb_area == MAP_FAILED) { printf("tlb: mmap failed\n"); return 1; }
    unsigned t0 = banana_ticks();
    int ids[16], bad = 0;
    g_seen = 0;
    for (int i = 0; i < n; i++) ids[i] = banana_thread(tlb_worker, (void*)(long)i);
    for (int i = 0; i < n; i++) {
        int got = ids[i] > 0 ? banana_join(ids[i]) : -1;
        if (got != 0) { printf("tlb thread %d: returned %d (want 0)\n", i, got); bad = 1; }
    }
    munmap(g_tlb_area, (size_t)n * 2 * 4096);
    int cores = 0;
    for (int k = 0; k < 64; k++) if (g_seen & (1ull << k)) cores++;
    printf("%d threads x %d rounds of mmap / mprotect / munmap: %s, on %d core%s, %u ms\n",
           n, TLB_ROUNDS, bad ? "FAILED" : "every fresh page zero", cores, cores == 1 ? "" : "s", banana_ticks() - t0);
    return bad ? 1 : 100 + cores;
}

/* page i of the swap test: zeros (1 in 4), text (compresses), or noise (1 in 8: does not) */
static void swap_page(unsigned char* p, unsigned i) {
    if (i % 4 == 0) { memset(p, 0, 4096); return; }
    if (i % 8 == 3) {
        unsigned x = i * 2654435761u + 1;
        for (int k = 0; k < 4096; k++) p[k] = (unsigned char)xs(&x);
        return;
    }
    int n = 0;
    while (n < 4096) n += snprintf((char*)p + n, 4096 - n, "page %u of the swap test, line %d. ", i, n / 40);
}

static int t_swap(int mib, int wait) {
    size_t len = (size_t)mib << 20;
    unsigned char* m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) { printf("swap: mmap of %d MiB failed\n", mib); return 1; }
    unsigned pages = (unsigned)(len / 4096);
    for (unsigned i = 0; i < pages; i++) swap_page(m + (size_t)i * 4096, i);
    printf("swap: %u pages written, waiting %d ms\n", pages, wait);
    banana_sleep((unsigned)wait);
    static unsigned char want[4096];
    unsigned bad = 0;
    for (unsigned i = 0; i < pages; i++) {
        swap_page(want, i);
        if (memcmp(m + (size_t)i * 4096, want, 4096)) bad++;
    }
    munmap(m, len);
    printf("swap: %u pages read back, %u wrong\n", pages, bad);
    return bad ? 1 : 0;
}

/* Keep actual user-mode work live across a kernel S3 cycle. The shell owns
 * unique ready/stop/result paths; a monotonic timeout also ends the helper. */
static volatile int pm_running,pm_started,pm_bad;
static int pm_worker(void* arg) {
    unsigned id=(unsigned)(uintptr_t)arg,n=0;
    volatile unsigned marker[128];
    for(unsigned i=0;i<128;i++)marker[i]=0x53330000u^(id<<12)^i;
    double initial=1.25+id,value=initial;
    __sync_add_and_fetch(&pm_started,1);
    while(__atomic_load_n(&pm_running,__ATOMIC_ACQUIRE)) {
        value+=0.5;n++;
        if(n==1000000) {
            if(value!=initial+n*0.5)__atomic_store_n(&pm_bad,1,__ATOMIC_RELEASE);
            value=initial;n=0;
        }
    }
    if(value!=initial+n*0.5)return 1;
    for(unsigned i=0;i<128;i++)if(marker[i]!=(0x53330000u^(id<<12)^i))return 1;
    return 0;
}
static int t_suspend(const char* path) {
    char ready[96],stop[96];snprintf(ready,sizeof(ready),"%s.ready",path);snprintf(stop,sizeof(stop),"%s.stop",path);
    int ids[8],n=banana_cpus();if(n<2)n=2;if(n>8)n=8;
    __atomic_store_n(&pm_running,1,__ATOMIC_RELEASE);unsigned start=banana_ticks();int bad=0;
    for(int i=0;i<n;i++){ids[i]=banana_thread(pm_worker,(void*)(uintptr_t)i);if(ids[i]<=0)bad=1;}
    while(__atomic_load_n(&pm_started,__ATOMIC_ACQUIRE)<n && banana_ticks()-start<2000)banana_sleep(10);
    if(__atomic_load_n(&pm_started,__ATOMIC_ACQUIRE)!=n)bad=1;
    FILE* f=fopen(ready,"w");if(f){fputs(bad?"failed":"ready",f);fclose(f);}else bad=1;
    while(banana_ticks()-start<10000) {
        f=fopen(stop,"r");if(f){fclose(f);break;}banana_sleep(10);
    }
    __atomic_store_n(&pm_running,0,__ATOMIC_RELEASE);
    for(int i=0;i<n;i++)if(ids[i]<=0 || banana_join(ids[i]))bad=1;
    bad|=__atomic_load_n(&pm_bad,__ATOMIC_ACQUIRE);
    f=fopen(path,"w");if(f){fputs(bad?"failed":"ok",f);fclose(f);}else bad=1;
    return bad;
}

/* ── malloc: the app's heap (sdk/lib/malloc.c) against the kernel's allocator ── */
extern const banana_api_t* __banana;
int __malloc_heap(void);

#define MB_SLOTS 256
#define MB_ROUNDS 200000

/* MB_ROUNDS malloc / free pairs over a window of live blocks (16 B .. 1 KiB),
 * through the heap (kernel = 0) or the kernel's allocator (1): milliseconds */
static unsigned bench(int kernel, unsigned seed) {
    void* live[MB_SLOTS] = { 0 };
    unsigned x = seed, t0 = banana_ticks();
    for (int i = 0; i < MB_ROUNDS; i++) {
        unsigned k = xs(&x) % MB_SLOTS;
        if (live[k]) { if (kernel) __banana->free(live[k]); else free(live[k]); }
        size_t n = 16 + xs(&x) % 1008;
        live[k] = kernel ? __banana->malloc(n) : malloc(n);
        if (live[k]) ((char*)live[k])[0] = 1;
    }
    for (int k = 0; k < MB_SLOTS; k++) if (live[k]) { if (kernel) __banana->free(live[k]); else free(live[k]); }
    return banana_ticks() - t0;
}

static volatile int g_mb_kernel;
static int bench_worker(void* arg) { return (int)bench(g_mb_kernel, 77u + (unsigned)(long)arg); }

/* all cores at once: the slowest thread's time */
static unsigned bench_all(int kernel, int n) {
    g_mb_kernel = kernel;
    int ids[16];
    unsigned worst = 0;
    for (int i = 0; i < n; i++) ids[i] = banana_thread(bench_worker, (void*)(long)i);
    for (int i = 0; i < n; i++) if (ids[i] > 0) { unsigned t = (unsigned)banana_join(ids[i]); if (t > worst) worst = t; }
    return worst;
}

static int t_malloc(void) {
    /* first: every byte of random blocks survives, realloc keeps data, alignment holds */
    int bad = 0;
    unsigned x = 4242;
    static unsigned char* blk[512];
    static size_t len[512];
    for (int i = 0; i < 50000 && !bad; i++) {
        int k = (int)(xs(&x) % 512);
        if (blk[k]) {
            for (size_t j = 0; j < len[k]; j++) if (blk[k][j] != (unsigned char)(k + j)) { bad = 1; break; }
            if (xs(&x) % 3 == 0) {
                size_t n = xs(&x) % 70000 + 1;
                unsigned char* q = realloc(blk[k], n);
                if (!q) { bad = 2; break; }
                for (size_t j = len[k] < n ? len[k] : n; j < n; j++) q[j] = (unsigned char)(k + j);
                blk[k] = q; len[k] = n;
                continue;
            }
            free(blk[k]);
            blk[k] = NULL;
        } else {
            len[k] = xs(&x) % (xs(&x) % 8 ? 600 : 70000) + 1;
            blk[k] = malloc(len[k]);
            if (!blk[k]) { bad = 2; break; }
            for (size_t j = 0; j < len[k]; j++) blk[k][j] = (unsigned char)(k + j);
        }
    }
    for (int k = 0; k < 512; k++) { free(blk[k]); blk[k] = NULL; }
    for (size_t a = 16; a <= 65536 && !bad; a *= 2) {
        void* p = NULL;
        if (posix_memalign(&p, a, 1000) || ((unsigned long)p % a)) bad = 3;
        free(p);
    }
    if (bad) { printf("malloc: %s\n", bad == 1 ? "a block changed" : bad == 2 ? "out of memory" : "a block was not aligned"); return 1; }

    /* then: how fast */
    int heap = __malloc_heap();
    int n = banana_cpus();
    if (n < 2) n = 2;
    if (n > 16) n = 16;
    unsigned h1 = bench(0, 1), k1 = bench(1, 1);
    unsigned hn = bench_all(0, n), kn = bench_all(1, n);
    unsigned s1 = h1 ? k1 / h1 : k1, sn = hn ? kn / hn : kn;
    printf("malloc: %s; %d malloc + free each: 1 thread %u ms (kernel's allocator %u ms), %d threads %u ms (kernel's %u ms)\n",
           heap ? "the app's own heap" : "the kernel's allocator (32-bit)", MB_ROUNDS, h1, k1, n, hn, kn);
    unsigned speed = s1 < sn ? s1 : sn;
    if (speed > 150) speed = 150;
    return 100 + (int)speed;                            /* (selftest: 100 + how many times faster) */
}

/* ── AVX: the YMM registers survive task switches, and a new thread finds
 * none of another's in them ─────────────────────────────────────────── */
#ifdef __x86_64__
static int avx_usable(void) {
    unsigned a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if (!((c >> 27) & 1) || !((c >> 28) & 1)) return 0;   /* OSXSAVE (the system turned it on), AVX */
    unsigned lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return (lo & 6) == 6;                                 /* SSE and AVX state saved by the system */
}

/* fills YMM0-15 from in (16 x 32 bytes), spins `spins` times touching only
 * a counter, then writes them back to out: a task switch that loses any
 * part of a YMM register shows in out */
static void ymm_hold(const unsigned char* in, unsigned char* out, unsigned long spins) {
    __asm__ volatile(
        "vmovdqu   0(%0), %%ymm0\n vmovdqu  32(%0), %%ymm1\n vmovdqu  64(%0), %%ymm2\n vmovdqu  96(%0), %%ymm3\n"
        "vmovdqu 128(%0), %%ymm4\n vmovdqu 160(%0), %%ymm5\n vmovdqu 192(%0), %%ymm6\n vmovdqu 224(%0), %%ymm7\n"
        "vmovdqu 256(%0), %%ymm8\n vmovdqu 288(%0), %%ymm9\n vmovdqu 320(%0), %%ymm10\n vmovdqu 352(%0), %%ymm11\n"
        "vmovdqu 384(%0), %%ymm12\n vmovdqu 416(%0), %%ymm13\n vmovdqu 448(%0), %%ymm14\n vmovdqu 480(%0), %%ymm15\n"
        "1: dec %2\n jnz 1b\n"
        "vmovdqu %%ymm0,   0(%1)\n vmovdqu %%ymm1,  32(%1)\n vmovdqu %%ymm2,  64(%1)\n vmovdqu %%ymm3,  96(%1)\n"
        "vmovdqu %%ymm4, 128(%1)\n vmovdqu %%ymm5, 160(%1)\n vmovdqu %%ymm6, 192(%1)\n vmovdqu %%ymm7, 224(%1)\n"
        "vmovdqu %%ymm8, 256(%1)\n vmovdqu %%ymm9, 288(%1)\n vmovdqu %%ymm10, 320(%1)\n vmovdqu %%ymm11, 352(%1)\n"
        "vmovdqu %%ymm12, 384(%1)\n vmovdqu %%ymm13, 416(%1)\n vmovdqu %%ymm14, 448(%1)\n vmovdqu %%ymm15, 480(%1)\n"
        "vzeroupper\n"
        : "+r"(in), "+r"(out), "+r"(spins)
        :
        : "memory", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
          "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
}

static volatile int g_avx_bad, g_avx_leak;

static int avx_worker(void* arg) {
    unsigned char in[512], out[512];
    unsigned x = 0xA5000000u + (unsigned)(long)arg * 7919u;
    for (int round = 0; round < 6; round++) {
        for (int i = 0; i < 512; i++) in[i] = (unsigned char)xs(&x);
        ymm_hold(in, out, 30000000ul);                    /* long enough to be switched out many times */
        if (memcmp(in, out, 512)) __sync_fetch_and_add(&g_avx_bad, 1);
    }
    return 0;
}

/* a new thread: are the upper halves of its YMM registers clean? */
static int avx_fresh(void* arg) {
    (void)arg;
    unsigned char up[16 * 16];
    __asm__ volatile(
        "vextractf128 $1, %%ymm0,   0(%0)\n vextractf128 $1, %%ymm1,  16(%0)\n vextractf128 $1, %%ymm2,  32(%0)\n"
        "vextractf128 $1, %%ymm3,  48(%0)\n vextractf128 $1, %%ymm4,  64(%0)\n vextractf128 $1, %%ymm5,  80(%0)\n"
        "vextractf128 $1, %%ymm6,  96(%0)\n vextractf128 $1, %%ymm7, 112(%0)\n vextractf128 $1, %%ymm8, 128(%0)\n"
        "vextractf128 $1, %%ymm9, 144(%0)\n vextractf128 $1, %%ymm10, 160(%0)\n vextractf128 $1, %%ymm11, 176(%0)\n"
        "vextractf128 $1, %%ymm12, 192(%0)\n vextractf128 $1, %%ymm13, 208(%0)\n vextractf128 $1, %%ymm14, 224(%0)\n"
        "vextractf128 $1, %%ymm15, 240(%0)\n"
        :: "r"(up) : "memory");
    for (int i = 0; i < 256; i++) if (up[i]) { __sync_fetch_and_add(&g_avx_leak, 1); break; }
    return 0;
}

static int t_avx(void) {
    if (!avx_usable()) { printf("avx: not usable here (no AVX, or the system did not turn it on)\n"); return 2; }
    int n = banana_cpus() * 2;
    if (n < 4) n = 4;
    if (n > 16) n = 16;
    int ids[16];
    unsigned t0 = banana_ticks();
    for (int i = 0; i < n; i++) ids[i] = banana_thread(avx_worker, (void*)(long)i);
    for (int i = 0; i < n; i++) if (ids[i] > 0) banana_join(ids[i]);
    unsigned ms = banana_ticks() - t0;
    for (int i = 0; i < n; i++) ids[i] = banana_thread(avx_fresh, NULL);
    for (int i = 0; i < n; i++) if (ids[i] > 0) banana_join(ids[i]);
    printf("avx: %d threads x 6 rounds holding YMM0-15 (%u ms): %d changed; %d new threads, %d found old AVX state\n",
           n, ms, g_avx_bad, n, g_avx_leak);
    return g_avx_bad ? 1 : g_avx_leak ? 3 : 0;
}
#else
static int t_avx(void) { printf("avx: the 32-bit build does not check it\n"); return 2; }
#endif

/* ── system calls: the syscall entry (stubs "mov r10, rcx; mov eax, slot;
 * syscall; ret") against int 0x80, which stays for comparison ─────────── */
#ifdef __x86_64__
#define SC_CALLS 200000

/* a call through the table keeps rbx and r12-r15 (callee-saved) */
static int sc_saved(void) {
    unsigned long bad;
    void* fn = (void*)__banana->ticks_ms;
    __asm__ volatile(
        "mov $0x1111111111111111, %%rbx\n mov $0x1212121212121212, %%r12\n mov $0x1313131313131313, %%r13\n"
        "mov $0x1414141414141414, %%r14\n mov $0x1515151515151515, %%r15\n"
        "call *%1\n"
        "xor %0, %0\n"
        "mov $0x1111111111111111, %%rax\n cmp %%rax, %%rbx\n setne %b0\n"
        "mov $0x1212121212121212, %%rax\n cmp %%rax, %%r12\n jne 1f\n"
        "mov $0x1313131313131313, %%rax\n cmp %%rax, %%r13\n jne 1f\n"
        "mov $0x1414141414141414, %%rax\n cmp %%rax, %%r14\n jne 1f\n"
        "mov $0x1515151515151515, %%rax\n cmp %%rax, %%r15\n jne 1f\n"
        "jmp 2f\n 1: mov $1, %0\n 2:\n"
        : "=&r"(bad), "+r"(fn)
        :
        : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "rbx", "r12", "r13", "r14", "r15", "memory", "cc");
    return bad ? 0 : 1;
}

/* draw_text: 9 arguments - the 4th (h) in r10 now, 3 on the stack */
static int sc_args(void) {
    static unsigned px[16 * 8];
    for (int i = 0; i < 16 * 8; i++) px[i] = 0x123456;
    __banana->draw_text(px, 16, 16, 5, 0, 0, "A", 0xFFFFFF, 0);    /* h = 5: rows 5..7 stay */
    const unsigned char* g = __banana->font8x8 + 'A' * 8;  /* (128 glyphs of 8 rows, bit 0 leftmost) */
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            unsigned want = y >= 5 ? 0x123456u : ((g[y] >> x) & 1) ? 0xFFFFFFu : 0u;
            if (px[y * 16 + x] != want) return 0;
        }
    return 1;
}

static int t_syscall(void) {
    int ok_saved = sc_saved(), ok_args = sc_args();
    unsigned slot = (unsigned)(__builtin_offsetof(banana_api_t, ticks_ms) / sizeof(void*));
    unsigned t0 = banana_ticks();
    for (int i = 0; i < SC_CALLS; i++) (void)__banana->ticks_ms();
    unsigned t_sc = banana_ticks() - t0;
    t0 = banana_ticks();
    for (int i = 0; i < SC_CALLS; i++) {
        unsigned long rax = slot;
        __asm__ volatile("int $0x80" : "+a"(rax) :: "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory");
    }
    unsigned t_int = banana_ticks() - t0;
    unsigned ratio10 = t_sc ? (t_int * 10u + t_sc / 2) / t_sc : 99u;   /* (tenths) */
    printf("syscall: registers kept %s, 9-argument call %s; %d calls: stubs %u ms, int 0x80 %u ms (%u.%ux)\n",
           ok_saved ? "yes" : "NO", ok_args ? "right" : "WRONG", SC_CALLS, t_sc, t_int, ratio10 / 10, ratio10 % 10);
    if (!ok_saved || !ok_args) return 1;
    if (ratio10 > 155) ratio10 = 155;
    return 100 + (int)ratio10;                          /* (selftest: 100 + tenths of how much faster) */
}
#else
static int t_syscall(void) { printf("syscall: the 32-bit build has no system call entry to check\n"); return 2; }
#endif

int main(int argc, char** argv) {
    const char* m = argc > 1 ? argv[1] : "";
    int a = argc > 2 ? atoi(argv[2]) : 0;
    if (!strcmp(m, "ring")) return t_ring();
    if (!strcmp(m, "mem")) return t_mem(a > 0 ? a : 64, argc > 3 && !strcmp(argv[3], "high"));
    if (!strcmp(m, "threads")) return t_threads(a > 0 ? a : 8);
    if (!strcmp(m, "files")) return t_files(a > 0 ? a : 8);
    if (!strcmp(m, "cores")) return t_cores();
    if (!strcmp(m, "suspend") && argc>2) return t_suspend(argv[2]);
    if (!strcmp(m, "idle")) { banana_sleep(a > 0 ? (unsigned)a : 1000); return 0; }
    if (!strcmp(m, "swap")) return t_swap(a > 0 ? a : 32, argc > 3 ? atoi(argv[3]) : 3000);
    if (!strcmp(m, "malloc")) return t_malloc();
    if (!strcmp(m, "avx")) return t_avx();
    if (!strcmp(m, "syscall")) return t_syscall();
    if (!strcmp(m, "tlb")) return t_tlb();
    printf("usage: systest ring | mem N [high] | threads N | files N | cores | idle MS | swap N MS | malloc | avx | syscall | tlb\n"
           "(the app half of the shell's `selftest`)\n");
    return 1;
}
