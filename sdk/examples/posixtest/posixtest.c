/* posixtest - checks the POSIX layer apps get from the SDK, for programs
 * ported from Linux (on the way to OpenJDK): one ok / FAIL line per check,
 * the exit code is how many failed. `selftest posix` runs it.
 *
 *   posixtest          every part
 *   posixtest mmap     mmap / munmap / mprotect
 *   posixtest threads  pthreads, __thread, semaphores, clocks
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <sys/mman.h>
#include <banana.h>

static int g_ok, g_fail;

static void check(const char* what, int ok, const char* note) {
    printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", what, note && note[0] ? " - " : "", note ? note : "");
    if (ok) g_ok++; else g_fail++;
}

/* ── mmap ─────────────────────────────────────────────────────────── */

static int all_zero(const char* p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i]) return 0;
    return 1;
}

#define TOUCH (4u << 20)
static char* g_area;

/* each thread fills its own 4 MiB of a fresh mapping, page by page */
static int toucher(void* arg) {
    long k = (long)arg;
    unsigned* p = (unsigned*)(g_area + (size_t)k * TOUCH);
    for (size_t i = 0; i < TOUCH / 4; i += 1024) p[i] = (unsigned)(k * 1000003u + i);
    for (size_t i = 0; i < TOUCH / 4; i += 1024) if (p[i] != (unsigned)(k * 1000003u + i)) return 1;
    return 0;
}

static void t_mmap(void) {
    printf("mmap / munmap / mprotect\n");
    int big = sizeof(void*) == 8;
    size_t reserve = big ? (1ul << 30) : (16ul << 20);
    char note[96];
    char* r = mmap(NULL, reserve, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    snprintf(note, sizeof(note), "%lu MiB at %p", (unsigned long)(reserve >> 20), (void*)r);
    check("reserve addresses (PROT_NONE)", r != MAP_FAILED, note);
    if (r == MAP_FAILED) return;

    size_t use = 8u << 20;
    check("mprotect 8 MiB of it read / write", mprotect(r, use, PROT_READ | PROT_WRITE) == 0, "");
    check("its pages read as zeros", all_zero(r, use), "");
    for (size_t i = 0; i < use; i += 4096) *(unsigned*)(r + i) = (unsigned)i * 2654435761u;
    int same = 1;
    for (size_t i = 0; i < use; i += 4096) if (*(unsigned*)(r + i) != (unsigned)i * 2654435761u) same = 0;
    check("written and read back, every page", same, "");

    /* a hole punched and mapped again (MAP_FIXED): fresh zero pages */
    char* mid = r + (2u << 20);
    memset(mid, 0xAB, 1u << 20);
    check("munmap 1 MiB in the middle", munmap(mid, 1u << 20) == 0, "");
    char* again = mmap(mid, 1u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    check("MAP_FIXED maps it again, zeroed", again == mid && all_zero(mid, 1u << 20), "");
    check("the pages around it kept their data", *(unsigned*)(r + (1u << 20)) == (1u << 20) * 2654435761u &&
          *(unsigned*)(r + (3u << 20)) == (3u << 20) * 2654435761u, "");

    if (big) {
        /* the far end of the reservation: only what is touched gets memory */
        char* end = r + reserve - (1u << 20);
        int ok = mprotect(end, 1u << 20, PROT_READ | PROT_WRITE) == 0;
        if (ok) { end[(1u << 20) - 1] = 42; ok = end[(1u << 20) - 1] == 42; }
        check("the last page of 1 GiB, touched alone", ok, "");
    }

    /* system calls given mapped memory: a file written from it, read back into it */
    int fd = open("/tmp/posixtest.dat", O_CREAT | O_TRUNC | O_RDWR);
    int io = fd >= 0 && write(fd, r, 100000) == 100000 && lseek(fd, 0, SEEK_SET) == 0;
    char* back = r + (5u << 20);
    io = io && read(fd, back, 100000) == 100000 && memcmp(back, r, 100000) == 0;
    check("write() and read() with a mapped buffer", io, "");

    /* a file mapped: its bytes, then zeros past its end */
    char* fm = fd >= 0 ? mmap(NULL, 200000, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    check("mmap a file", fm != MAP_FAILED && memcmp(fm, r, 100000) == 0 && all_zero(fm + 100000, 100000), "");
    if (fm != MAP_FAILED) munmap(fm, 200000);
    if (fd >= 0) close(fd);
    unlink("/tmp/posixtest.dat");

    /* several threads faulting pages in at once (on every core) */
    int n = banana_cpus() < 2 ? 2 : banana_cpus() > 8 ? 8 : banana_cpus();
    g_area = mmap(NULL, (size_t)n * TOUCH, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int ids[8], bad = g_area == MAP_FAILED;
    for (int k = 0; k < n && !bad; k++) ids[k] = banana_thread(toucher, (void*)(long)k);
    for (int k = 0; k < n && !bad; k++) if (ids[k] < 0 || banana_join(ids[k]) != 0) bad = 1;
    snprintf(note, sizeof(note), "%d threads, %d MiB", n, n * (int)(TOUCH >> 20));
    check("threads touching new pages at once", !bad, note);
    if (g_area != MAP_FAILED) munmap(g_area, (size_t)n * TOUCH);

    check("munmap the whole reservation", munmap(r, reserve) == 0, "");
}

/* ── threads ──────────────────────────────────────────────────────── */

static long ms_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void* ret_arg(void* a) { return (void*)((long)a * 3); }

static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static long g_count;
#define ADDS 20000

static void* adder(void* a) {
    (void)a;
    for (int i = 0; i < ADDS; i++) {
        pthread_mutex_lock(&g_m);
        g_count++;
        pthread_mutex_unlock(&g_m);
    }
    return NULL;
}

/* a bounded queue: a producer, a consumer, two condition variables */
#define QN 8
static int g_q[QN], g_qh, g_qt, g_qn;
static pthread_mutex_t g_qm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_notfull = PTHREAD_COND_INITIALIZER, g_notempty = PTHREAD_COND_INITIALIZER;
#define ITEMS 2000

static void* producer(void* a) {
    (void)a;
    for (int i = 1; i <= ITEMS; i++) {
        pthread_mutex_lock(&g_qm);
        while (g_qn == QN) pthread_cond_wait(&g_notfull, &g_qm);
        g_q[g_qt] = i; g_qt = (g_qt + 1) % QN; g_qn++;
        pthread_cond_signal(&g_notempty);
        pthread_mutex_unlock(&g_qm);
    }
    return NULL;
}

static void* consumer(void* a) {
    (void)a;
    long sum = 0;
    for (int i = 0; i < ITEMS; i++) {
        pthread_mutex_lock(&g_qm);
        while (!g_qn) pthread_cond_wait(&g_notempty, &g_qm);
        sum += g_q[g_qh]; g_qh = (g_qh + 1) % QN; g_qn--;
        pthread_cond_signal(&g_notfull);
        pthread_mutex_unlock(&g_qm);
    }
    return (void*)sum;
}

#ifdef __x86_64__
static __thread int t_var = 42;          /* (an initial value: the TLS image) */
static __thread char t_big[3000];        /* (zeroed: .tbss) */
static void* tls_user(void* a) {
    long k = (long)a;
    if (t_var != 42 || t_big[2999]) return (void*)1;
    t_var = (int)k * 7;
    t_big[2999] = (char)k;
    for (int i = 0; i < 2000; i++) { if (i % 100 == 0) sched_yield(); if (t_var != (int)k * 7) return (void*)2; }
    return t_big[2999] == (char)k ? NULL : (void*)3;
}
#endif

static pthread_key_t g_key;
static volatile int g_dtors;
static void key_dtor(void* v) { if (v == (void*)0x1234) __sync_fetch_and_add(&g_dtors, 1); }
static void* key_user(void* a) {
    (void)a;
    if (pthread_getspecific(g_key)) return (void*)1;
    pthread_setspecific(g_key, (void*)0x1234);
    return pthread_getspecific(g_key) == (void*)0x1234 ? NULL : (void*)2;
}

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static volatile int g_once_runs;
static void once_fn(void) { __sync_fetch_and_add(&g_once_runs, 1); banana_sleep(20); }
static void* once_user(void* a) { (void)a; pthread_once(&g_once, once_fn); return g_once_runs == 1 ? NULL : (void*)1; }

static volatile int g_detached_done;
static void* detached_fn(void* a) { (void)a; __sync_fetch_and_add(&g_detached_done, 1); return NULL; }

static void* deep_stack(void* a) {
    (void)a;
    volatile char big[3u << 20];                 /* 3 MiB of locals: needs the 4 MiB stack */
    big[0] = 1; big[sizeof(big) - 1] = 2;
    for (size_t i = 0; i < sizeof(big); i += 4096) big[i] = (char)i;
    pthread_attr_t at;
    size_t sz = 0;
    void* lo = NULL;
    if (pthread_getattr_np(pthread_self(), &at) == 0) pthread_attr_getstack(&at, &lo, &sz);
    char* here = (char*)&at;
    return sz >= (4u << 20) && here >= (char*)lo && here < (char*)lo + sz ? NULL : (void*)1;
}

static sem_t g_sem;
static void* sem_poster(void* a) { (void)a; for (int i = 0; i < 50; i++) { banana_sleep(1); sem_post(&g_sem); } return NULL; }

static pthread_barrier_t g_bar;
static volatile int g_bar_phase[4];
static void* bar_user(void* a) {
    long k = (long)a;
    for (int r = 0; r < 3; r++) {
        g_bar_phase[k] = r;
        pthread_barrier_wait(&g_bar);
        for (int j = 0; j < 4; j++) if (g_bar_phase[j] < r) return (void*)1;
        pthread_barrier_wait(&g_bar);
    }
    return NULL;
}

static pthread_rwlock_t g_rw = PTHREAD_RWLOCK_INITIALIZER;
static volatile int g_readers_in, g_rw_bad;
static int g_shared[2];
static void* rw_reader(void* a) {
    (void)a;
    for (int i = 0; i < 300; i++) {
        pthread_rwlock_rdlock(&g_rw);
        __sync_fetch_and_add(&g_readers_in, 1);
        if (g_shared[0] != g_shared[1]) g_rw_bad = 1;
        __sync_fetch_and_add(&g_readers_in, -1);
        pthread_rwlock_unlock(&g_rw);
    }
    return NULL;
}
static void* rw_writer(void* a) {
    (void)a;
    for (int i = 0; i < 300; i++) {
        pthread_rwlock_wrlock(&g_rw);
        if (g_readers_in) g_rw_bad = 1;
        g_shared[0]++; sched_yield(); g_shared[1]++;
        pthread_rwlock_unlock(&g_rw);
    }
    return NULL;
}

static void* errno_user(void* a) { (void)a; errno = EIO; sched_yield(); return errno == EIO ? NULL : (void*)1; }

static void t_threads(void) {
    printf("pthreads, __thread, semaphores, clocks\n");
    char note[96];
    pthread_t th[8];
    void* r;
    int ok = 1;
    for (long i = 0; i < 8; i++) if (pthread_create(&th[i], NULL, ret_arg, (void*)i)) ok = 0;
    for (long i = 0; i < 8 && ok; i++) if (pthread_join(th[i], &r) || r != (void*)(i * 3)) ok = 0;
    check("pthread_create / pthread_join, 8 threads and their results", ok, "");

    int n = banana_cpus() < 2 ? 4 : banana_cpus() * 2 > 8 ? 8 : banana_cpus() * 2;
    g_count = 0;
    for (int i = 0; i < n; i++) pthread_create(&th[i], NULL, adder, NULL);
    for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
    snprintf(note, sizeof(note), "%d threads x %d: %ld", n, ADDS, g_count);
    check("a mutex: no increment lost", g_count == (long)n * ADDS, note);

    pthread_mutexattr_t ma;
    pthread_mutex_t rm, em;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rm, &ma);
    ok = pthread_mutex_lock(&rm) == 0 && pthread_mutex_lock(&rm) == 0 && pthread_mutex_unlock(&rm) == 0 &&
         pthread_mutex_unlock(&rm) == 0 && pthread_mutex_trylock(&rm) == 0 && pthread_mutex_unlock(&rm) == 0;
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&em, &ma);
    ok = ok && pthread_mutex_lock(&em) == 0 && pthread_mutex_lock(&em) == EDEADLK && pthread_mutex_unlock(&em) == 0 &&
         pthread_mutex_unlock(&em) == EPERM;
    check("recursive and error-checking mutexes", ok, "");

    pthread_create(&th[0], NULL, producer, NULL);
    pthread_create(&th[1], NULL, consumer, NULL);
    pthread_join(th[0], NULL);
    pthread_join(th[1], &r);
    snprintf(note, sizeof(note), "%d items through 8 slots", ITEMS);
    check("condition variables: producer and consumer", (long)r == (long)ITEMS * (ITEMS + 1) / 2, note);

    pthread_condattr_t ca;
    pthread_cond_t cv;
    pthread_mutex_t cm = PTHREAD_MUTEX_INITIALIZER;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&cv, &ca);
    struct timespec until;
    clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_nsec += 150 * 1000000L;
    if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
    long t0 = ms_now();
    pthread_mutex_lock(&cm);
    int rc = pthread_cond_timedwait(&cv, &cm, &until);
    pthread_mutex_unlock(&cm);
    long took = ms_now() - t0;
    snprintf(note, sizeof(note), "%ld ms for 150", took);
    check("pthread_cond_timedwait times out (CLOCK_MONOTONIC)", rc == ETIMEDOUT && took >= 140 && took < 1000, note);

#ifdef __x86_64__
    ok = t_var == 42 && !t_big[2999];
    for (long i = 0; i < 6; i++) pthread_create(&th[i], NULL, tls_user, (void*)(i + 1));
    for (int i = 0; i < 6; i++) { pthread_join(th[i], &r); if (r) ok = 0; }
    check("__thread variables: each thread its own, initial values", ok && t_var == 42, "");
#else
    printf("  skip  __thread variables - the 32-bit system has no thread pointer\n");
#endif

    pthread_key_create(&g_key, key_dtor);
    g_dtors = 0;
    ok = 1;
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, key_user, NULL);
    for (int i = 0; i < 4; i++) { pthread_join(th[i], &r); if (r) ok = 0; }
    check("keys: a value per thread, destructors at exit", ok && g_dtors == 4 && !pthread_getspecific(g_key), "");

    ok = 1;
    for (int i = 0; i < 6; i++) pthread_create(&th[i], NULL, once_user, NULL);
    for (int i = 0; i < 6; i++) { pthread_join(th[i], &r); if (r) ok = 0; }
    check("pthread_once runs once for 6 threads", ok && g_once_runs == 1, "");

    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    g_detached_done = 0;
    for (int made = 0, tries = 0; made < 150 && tries < 1000; tries++) {
        pthread_t d;
        if (pthread_create(&d, &at, detached_fn, NULL) == 0) made++;
        else banana_sleep(5);                       /* (every slot busy: wait for some to end) */
    }
    long until_ms = ms_now() + 10000;
    while (g_detached_done < 150 && ms_now() < until_ms) banana_sleep(5);
    snprintf(note, sizeof(note), "%d of 150 ran (more than the 64 threads at once)", g_detached_done);
    check("detached threads: their slots come back", g_detached_done == 150, note);

    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 4u << 20);
    r = (void*)1;
    if (pthread_create(&th[0], &at, deep_stack, NULL) == 0) pthread_join(th[0], &r);
    check("a 4 MiB stack (pthread_attr_setstacksize), 3 MiB of locals", r == NULL, "");

    sem_init(&g_sem, 0, 0);
    pthread_create(&th[0], NULL, sem_poster, NULL);
    ok = 1;
    for (int i = 0; i < 50; i++) if (sem_wait(&g_sem)) ok = 0;
    pthread_join(th[0], NULL);
    int v = -1;
    sem_getvalue(&g_sem, &v);
    check("semaphores: 50 posts, 50 waits", ok && v == 0 && sem_trywait(&g_sem) == -1 && errno == EAGAIN, "");

    pthread_barrier_init(&g_bar, NULL, 4);
    ok = 1;
    for (long i = 0; i < 4; i++) pthread_create(&th[i], NULL, bar_user, (void*)i);
    for (int i = 0; i < 4; i++) { pthread_join(th[i], &r); if (r) ok = 0; }
    check("a barrier: 4 threads, 3 rounds", ok, "");

    g_rw_bad = 0;
    for (int i = 0; i < 3; i++) pthread_create(&th[i], NULL, rw_reader, NULL);
    pthread_create(&th[3], NULL, rw_writer, NULL);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    check("a read-write lock: readers never see a half write", !g_rw_bad && g_shared[0] == 300, "");

    errno = 0;
    pthread_create(&th[0], NULL, errno_user, NULL);
    pthread_join(th[0], &r);
    check("errno is per thread", r == NULL && errno == 0, "");

    t0 = ms_now();
    struct timespec nap = { 0, 60 * 1000000L };
    nanosleep(&nap, NULL);
    took = ms_now() - t0;
    snprintf(note, sizeof(note), "%ld ms for 60", took);
    check("nanosleep and CLOCK_MONOTONIC", took >= 55 && took < 1000, note);

    long cpus = sysconf(_SC_NPROCESSORS_ONLN), pages = sysconf(_SC_PHYS_PAGES);
    cpu_set_t set;
    sched_getaffinity(0, sizeof(set), &set);
    snprintf(note, sizeof(note), "%ld cores, %ld MiB, pid %d", cpus, pages / 256, (int)getpid());
    check("sysconf, sched_getaffinity, getpid", cpus >= 1 && pages > 1000 && CPU_COUNT(&set) == cpus && getpid() > 0, note);
}

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : NULL;
    if (!only || !strcmp(only, "mmap")) t_mmap();
    if (!only || !strcmp(only, "threads")) t_threads();
    printf("posixtest: %d passed, %d failed\n", g_ok, g_fail);
    return g_fail;
}
