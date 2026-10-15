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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <banana.h>

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

int main(int argc, char** argv) {
    const char* m = argc > 1 ? argv[1] : "";
    int a = argc > 2 ? atoi(argv[2]) : 0;
    if (!strcmp(m, "ring")) return t_ring();
    if (!strcmp(m, "mem")) return t_mem(a > 0 ? a : 64, argc > 3 && !strcmp(argv[3], "high"));
    if (!strcmp(m, "threads")) return t_threads(a > 0 ? a : 8);
    if (!strcmp(m, "files")) return t_files(a > 0 ? a : 8);
    if (!strcmp(m, "cores")) return t_cores();
    if (!strcmp(m, "idle")) { banana_sleep(a > 0 ? (unsigned)a : 1000); return 0; }
    printf("usage: systest ring | mem N [high] | threads N | files N | cores | idle MS\n"
           "(the app half of the shell's `selftest`)\n");
    return 1;
}
