/* Banana OS SDK: POSIX threads (<pthread.h>, <semaphore.h>, <sched.h>) over
 * the system's threads (thread_start, API version 17) and its wait / wake.
 *
 * Every thread - pthread_create's, banana_thread's and the main one - has a
 * struct __pthread; on the 64-bit system it sits at the thread pointer (FS)
 * with the thread's TLS block (the program's PT_TLS image) just under it,
 * the x86_64 layout, so __thread variables work. Stacks are mapped (mmap)
 * with a PROT_NONE guard under them. A thread's stack and record are given
 * back when it is joined, or - detached - by the next pthread_create. */
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "banana_api.h"

extern const banana_api_t* __banana;

#ifdef __TINYC__
static int tcc_xchg(volatile int* p, int v) { __asm__ volatile("xchgl %0, %1" : "+r"(v), "+m"(*p) : : "memory"); return v; }
static int tcc_xadd(volatile int* p, int v) { __asm__ volatile("lock xaddl %0, %1" : "+r"(v), "+m"(*p) : : "memory"); return v; }
static int tcc_cas(volatile int* p, int o, int n) {
    int prev;
    __asm__ volatile("lock cmpxchgl %2, %1" : "=a"(prev), "+m"(*p) : "r"(n), "0"(o) : "memory");
    return prev;
}
#define XCHG(p, v)    tcc_xchg((p), (v))
#define XADD(p, v)    tcc_xadd((p), (v))
#define CAS(p, o, n)  tcc_cas((p), (o), (n))
#else
#define XCHG(p, v)    __sync_lock_test_and_set((p), (v))
#define XADD(p, v)    __sync_fetch_and_add((p), (v))
#define CAS(p, o, n)  __sync_val_compare_and_swap((p), (o), (n))
#endif

#define DEFAULT_STACK (1ul << 20)
#define PAGE          4096ul
#define IDS           130            /* system thread ids: 0 (main) .. 64, with room */

struct __pthread {
    struct __pthread* self;           /* %fs:0 (the x86_64 TLS ABI) */
    void*        mem;                 /* what was allocated: TLS block + this */
    volatile int id;                  /* the system's thread id (0: main) */
    void*      (*fn)(void*);
    int        (*rawfn)(void*);       /* banana_thread's: an int result */
    void*        arg;
    void*        ret;
    int          rawret;
    volatile int done;                /* 1 once it ended (joiners wait on it) */
    int          detached;
    int          raw;
    void*        stack;               /* its usable stack: [stack, stack + stack_size) */
    size_t       stack_size, guard;
    void*        map;                 /* mapped here (unmapped when it is gone), or NULL */
    size_t       map_len;
    void*        keys[PTHREAD_KEYS_MAX];
    struct __pthread_cleanup* cleanup;
    char         name[16];
    struct __pthread* next;           /* (the list of ended detached threads) */
};

static struct __pthread  g_main;
static struct __pthread* g_by_id[IDS];
static int               g_tls_ok;    /* threads find themselves through FS */
static const unsigned char* g_tls_image;
static size_t            g_tls_filesz, g_tls_memsz, g_tls_align = 1;
static volatile int      g_life;      /* guards done/detached and the list below */
static struct __pthread* g_dead;      /* ended detached threads, waiting to be freed */
unsigned long            __banana_stack_top;   /* crt0: the main thread's stack */

static int has_v17(void) {
    return __banana->version >= 17 && BANANA_HAS(__banana, app_id);
}
static int has_wait(void) { return __banana->version >= 4; }

static void life_lock(void) { while (XCHG(&g_life, 1)) __banana->yield(); }
static void life_unlock(void) { g_life = 0; }

static int fwait(volatile int* a, int v, int ms) {
    if (has_wait()) return __banana->wait(a, v, ms);
    unsigned end = __banana->ticks_ms() + (unsigned)(ms > 0 ? ms : 0);
    while (*a == v) {
        if (ms >= 0 && (int)(__banana->ticks_ms() - end) >= 0) return 1;
        __banana->sleep_ms(1);
    }
    return 0;
}
static void fwake(volatile int* a, int n) { if (has_wait()) __banana->wake(a, n); }

/* ── TLS ──────────────────────────────────────────────────────────── */

#ifdef __x86_64__
/* the program's own ELF header, where it was loaded (GNU ld defines it) */
extern const unsigned char __ehdr_start[] __attribute__((weak, visibility("hidden")));

static void tls_find(void) {
    const unsigned char* e = __ehdr_start;
    if (!e || memcmp(e, "\177ELF", 4) || e[4] != 2) return;
    uint64_t phoff = *(const uint64_t*)(e + 32);
    uint16_t phentsize = *(const uint16_t*)(e + 54), phnum = *(const uint16_t*)(e + 56);
    for (unsigned i = 0; i < phnum; i++) {
        const unsigned char* ph = e + phoff + (uint64_t)i * phentsize;
        if (*(const uint32_t*)ph != 7) continue;               /* PT_TLS */
        g_tls_image = e + *(const uint64_t*)(ph + 16);         /* p_vaddr (the image is linked at 0) */
        g_tls_filesz = (size_t)*(const uint64_t*)(ph + 32);
        g_tls_memsz = (size_t)*(const uint64_t*)(ph + 40);
        g_tls_align = (size_t)*(const uint64_t*)(ph + 48);
        if (g_tls_align < 1) g_tls_align = 1;
    }
}
#else
static void tls_find(void) {}
#endif

/* a record for a new thread, its TLS block (zeroed, the image copied) under it */
static struct __pthread* record_new(void) {
    size_t align = g_tls_align < 64 ? 64 : g_tls_align;
    size_t tls = (g_tls_memsz + g_tls_align - 1) & ~(g_tls_align - 1);
    unsigned char* mem = calloc(1, tls + sizeof(struct __pthread) + align);
    if (!mem) return NULL;
    uintptr_t tp = ((uintptr_t)mem + tls + align - 1) & ~(uintptr_t)(align - 1);
    if (g_tls_image && g_tls_filesz) memcpy((void*)(tp - tls), g_tls_image, g_tls_filesz);
    struct __pthread* t = (struct __pthread*)tp;
    t->self = t;
    t->mem = mem;
    return t;
}

/* the calling thread is t from now on */
static void install(struct __pthread* t) {
    int id = __banana->version >= 3 ? __banana->thread_id() : 0;
    t->id = id;
    if (id >= 0 && id < IDS) g_by_id[id] = t;
    if (g_tls_ok) __banana->tls_set(t);
}

void __pthread_init(void) {
    tls_find();
    /* the main thread's record: with a TLS block under it if the program has
     * __thread variables */
    struct __pthread* t = g_tls_memsz ? record_new() : NULL;
    if (!t) t = &g_main;
    t->self = t;
    /* the main stack: crt0 kept where it began (the system gives 1 MiB) */
    if (__banana_stack_top) {
        uintptr_t top = (__banana_stack_top + PAGE - 1) & ~(PAGE - 1);
        t->stack = (void*)(top - DEFAULT_STACK);
        t->stack_size = DEFAULT_STACK;
        t->guard = 16384;
    }
    strcpy(t->name, "main");
    g_tls_ok = has_v17() && __banana->tls_set(t) == 0;
    install(t);
}

pthread_t pthread_self(void) {
#ifdef __x86_64__
    if (g_tls_ok) {
        struct __pthread* s;
        __asm__ volatile("mov %%fs:0, %0" : "=r"(s));
        return s;
    }
#endif
    int id = __banana->version >= 3 ? __banana->thread_id() : 0;
    struct __pthread* t = id >= 0 && id < IDS ? g_by_id[id] : NULL;
    if (t) return t;
    t = record_new();                    /* a thread the SDK did not start: adopted */
    if (!t) return &g_main;
    install(t);
    return t;
}

int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

/* ── keys ─────────────────────────────────────────────────────────── */

static struct { volatile int used; void (*dtor)(void*); } g_keys[PTHREAD_KEYS_MAX];

int pthread_key_create(pthread_key_t* k, void (*dtor)(void*)) {
    for (unsigned i = 0; i < PTHREAD_KEYS_MAX; i++) {
        if (CAS(&g_keys[i].used, 0, 1) == 0) {
            g_keys[i].dtor = dtor;
            *k = i;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t k) {
    if (k >= PTHREAD_KEYS_MAX || !g_keys[k].used) return EINVAL;
    g_keys[k].dtor = NULL;
    g_keys[k].used = 0;
    return 0;
}

void* pthread_getspecific(pthread_key_t k) { return k < PTHREAD_KEYS_MAX ? pthread_self()->keys[k] : NULL; }

int pthread_setspecific(pthread_key_t k, const void* v) {
    if (k >= PTHREAD_KEYS_MAX || !g_keys[k].used) return EINVAL;
    pthread_self()->keys[k] = (void*)v;
    return 0;
}

static void run_destructors(struct __pthread* t) {
    for (int round = 0; round < PTHREAD_DESTRUCTOR_ITERATIONS; round++) {
        int any = 0;
        for (unsigned k = 0; k < PTHREAD_KEYS_MAX; k++) {
            void* v = t->keys[k];
            void (*d)(void*) = g_keys[k].dtor;
            if (!v || !d || !g_keys[k].used) continue;
            t->keys[k] = NULL;
            d(v);
            any = 1;
        }
        if (!any) break;
    }
}

/* ── starting and ending threads ──────────────────────────────────── */

static void release(struct __pthread* t) {
    if (t->map) munmap(t->map, t->map_len);
    if (t->id >= 0 && t->id < IDS && g_by_id[t->id] == t) g_by_id[t->id] = NULL;
    free(t->mem);
}

/* the detached threads that ended: their system thread is waited for (it is
 * about to end), then their stack and record go */
static void reap(void) {
    life_lock();
    struct __pthread* d = g_dead;
    g_dead = NULL;
    life_unlock();
    while (d) {
        struct __pthread* next = d->next;
        __banana->thread_join(d->id);
        release(d);
        d = next;
    }
}

/* t ended (returned, or pthread_exit) */
static void finish(struct __pthread* t) {
    run_destructors(t);
    life_lock();
    t->done = 1;
    if (t->detached) { t->next = g_dead; g_dead = t; }
    life_unlock();
    fwake(&t->done, 0);
}

static int start(void* arg) {
    struct __pthread* t = arg;
    install(t);
    if (t->raw) t->rawret = t->rawfn(t->arg);
    else t->ret = t->fn(t->arg);
    int r = t->rawret;
    finish(t);
    return r;
}

/* a new system thread running t (stack from attr, or mapped here) */
static int spawn(struct __pthread* t, const pthread_attr_t* a) {
    size_t size = a && a->stack_size ? a->stack_size : DEFAULT_STACK;
    if (size < PTHREAD_STACK_MIN) size = PTHREAD_STACK_MIN;
    size = (size + PAGE - 1) & ~(PAGE - 1);
    if (a && a->stack) {
        t->stack = a->stack;
        t->stack_size = a->stack_size;
    } else if (has_v17()) {
        size_t guard = a ? (a->guard_size + PAGE - 1) & ~(PAGE - 1) : PAGE;
        void* m = mmap(NULL, size + guard, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (m == MAP_FAILED) return EAGAIN;
        if (guard) mprotect(m, guard, PROT_NONE);
        t->map = m;
        t->map_len = size + guard;
        t->stack = (char*)m + guard;
        t->stack_size = size;
        t->guard = guard;
    }
    int id = has_v17() ? __banana->thread_start(start, t, t->stack, t->stack ? t->stack_size : size)
                       : __banana->thread_create(start, t);
    if (id < 0) {
        if (t->map) munmap(t->map, t->map_len);
        t->map = NULL;
        return EAGAIN;
    }
    if (!t->id) t->id = id;          /* (it may have set it itself already) */
    return 0;
}

int pthread_create(pthread_t* out, const pthread_attr_t* a, void* (*fn)(void*), void* arg) {
    if (!out || !fn) return EINVAL;
    reap();
    struct __pthread* t = record_new();
    if (!t) return EAGAIN;
    t->fn = fn;
    t->arg = arg;
    t->detached = a && a->detached;
    t->id = 0;
    int rc = spawn(t, a);
    if (rc) { free(t->mem); return rc; }
    *out = t;
    return 0;
}

/* banana_thread() / banana_join() (banana.c): the same threads, an int result.
 * (By the id the creator got: the system hands a thread's id out again once
 * it is joined, so the thread's own slot in g_by_id may be a newer one's.) */
static struct __pthread* g_raw[IDS];

int __pthread_raw_create(int (*fn)(void*), void* arg) {
    reap();
    struct __pthread* t = record_new();
    if (!t) return -1;
    t->raw = 1;
    t->rawfn = fn;
    t->arg = arg;
    if (spawn(t, NULL)) { free(t->mem); return -1; }
    int id = t->id;
    if (id > 0 && id < IDS) g_raw[id] = t;
    return id;
}

int __pthread_raw_join(int id) {
    struct __pthread* t = NULL;
    if (id > 0 && id < IDS) { t = g_raw[id]; g_raw[id] = NULL; }
    int r = __banana->thread_join(id);       /* (start() returned the thread's int) */
    if (t) release(t);
    return r;
}

int pthread_join(pthread_t t, void** ret) {
    if (!t) return ESRCH;
    if (t == pthread_self()) return EDEADLK;
    if (t->detached) return EINVAL;
    while (!t->done) fwait(&t->done, 0, -1);
    if (t->id > 0) __banana->thread_join(t->id);
    if (ret) *ret = t->ret;
    if (t != &g_main) release(t);
    return 0;
}

int pthread_detach(pthread_t t) {
    if (!t) return ESRCH;
    life_lock();
    if (t->detached) { life_unlock(); return EINVAL; }
    t->detached = 1;
    if (t->done) { t->next = g_dead; g_dead = t; }     /* (it ended already: freed with the others) */
    life_unlock();
    return 0;
}

void __pthread_cleanup_push(struct __pthread_cleanup* c, void (*fn)(void*), void* arg) {
    struct __pthread* t = pthread_self();
    c->fn = fn;
    c->arg = arg;
    c->next = t->cleanup;
    t->cleanup = c;
}

void __pthread_cleanup_pop(struct __pthread_cleanup* c, int run) {
    struct __pthread* t = pthread_self();
    if (t->cleanup == c) t->cleanup = c->next;
    if (run) c->fn(c->arg);
}

void pthread_exit(void* ret) {
    struct __pthread* t = pthread_self();
    t->ret = ret;
    while (t->cleanup) {
        struct __pthread_cleanup* c = t->cleanup;
        t->cleanup = c->next;
        c->fn(c->arg);
    }
    finish(t);
    __banana->thread_exit(0);        /* (the main thread: the app ends) */
    for (;;) __banana->sleep_ms(1000);
}

int pthread_yield(void) { __banana->yield(); return 0; }
int sched_yield(void) { __banana->yield(); return 0; }

int pthread_setname_np(pthread_t t, const char* name) {
    if (!t || !name) return EINVAL;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = 0;
    return 0;
}

int pthread_getname_np(pthread_t t, char* name, size_t len) {
    if (!t || !name || len < strlen(t->name) + 1) return ERANGE;
    strcpy(name, t->name);
    return 0;
}

int pthread_getattr_np(pthread_t t, pthread_attr_t* a) {
    if (!t || !a) return EINVAL;
    pthread_attr_init(a);
    a->detached = t->detached;
    a->stack = t->stack;
    a->stack_size = t->stack_size;
    a->guard_size = t->guard;
    return 0;
}

int pthread_getcpuclockid(pthread_t t, clockid_t* clk) { (void)t; if (clk) *clk = CLOCK_THREAD_CPUTIME_ID; return 0; }
int pthread_setschedparam(pthread_t t, int policy, const struct sched_param* p) { (void)t; (void)policy; (void)p; return 0; }
int pthread_getschedparam(pthread_t t, int* policy, struct sched_param* p) {
    (void)t;
    if (policy) *policy = SCHED_OTHER;
    if (p) p->sched_priority = 0;
    return 0;
}
int pthread_setcancelstate(int state, int* old) { (void)state; if (old) *old = PTHREAD_CANCEL_ENABLE; return 0; }
int pthread_setcanceltype(int type, int* old) { (void)type; if (old) *old = PTHREAD_CANCEL_DEFERRED; return 0; }
void pthread_testcancel(void) {}
int pthread_cancel(pthread_t t) { (void)t; return ENOSYS; }
int pthread_atfork(void (*a)(void), void (*b)(void), void (*c)(void)) { (void)a; (void)b; (void)c; return 0; }

/* ── attributes ───────────────────────────────────────────────────── */

int pthread_attr_init(pthread_attr_t* a) { memset(a, 0, sizeof(*a)); a->guard_size = PAGE; return 0; }
int pthread_attr_destroy(pthread_attr_t* a) { (void)a; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t* a, int s) { if (s != 0 && s != 1) return EINVAL; a->detached = s; return 0; }
int pthread_attr_getdetachstate(const pthread_attr_t* a, int* s) { *s = a->detached; return 0; }
int pthread_attr_setstacksize(pthread_attr_t* a, size_t n) { if (n < PTHREAD_STACK_MIN) return EINVAL; a->stack_size = n; return 0; }
int pthread_attr_getstacksize(const pthread_attr_t* a, size_t* n) { *n = a->stack_size ? a->stack_size : DEFAULT_STACK; return 0; }
int pthread_attr_setguardsize(pthread_attr_t* a, size_t n) { a->guard_size = n; return 0; }
int pthread_attr_getguardsize(const pthread_attr_t* a, size_t* n) { *n = a->guard_size; return 0; }
int pthread_attr_setstack(pthread_attr_t* a, void* s, size_t n) {
    if (n < PTHREAD_STACK_MIN) return EINVAL;
    a->stack = s;
    a->stack_size = n;
    return 0;
}
int pthread_attr_getstack(const pthread_attr_t* a, void** s, size_t* n) { *s = a->stack; *n = a->stack_size; return 0; }
int pthread_attr_setschedpolicy(pthread_attr_t* a, int p) { a->sched_policy = p; return 0; }
int pthread_attr_getschedpolicy(const pthread_attr_t* a, int* p) { *p = a->sched_policy; return 0; }
int pthread_attr_setschedparam(pthread_attr_t* a, const struct sched_param* p) { a->sched_priority = p->sched_priority; return 0; }
int pthread_attr_getschedparam(const pthread_attr_t* a, struct sched_param* p) { p->sched_priority = a->sched_priority; return 0; }
int pthread_attr_setinheritsched(pthread_attr_t* a, int i) { a->inherit = i; return 0; }
int pthread_attr_getinheritsched(const pthread_attr_t* a, int* i) { *i = a->inherit; return 0; }
int pthread_attr_setscope(pthread_attr_t* a, int s) { a->scope = s; return 0; }
int pthread_attr_getscope(const pthread_attr_t* a, int* s) { *s = a->scope; return 0; }

/* ── time limits ──────────────────────────────────────────────────── */

/* milliseconds until abstime on clock clk (rounded up), 0 if it has passed */
static int ms_until(clockid_t clk, const struct timespec* abstime) {
    struct timespec now;
    clock_gettime(clk, &now);
    long long ms = (long long)(abstime->tv_sec - now.tv_sec) * 1000 + (abstime->tv_nsec - now.tv_nsec + 999999) / 1000000;
    return ms <= 0 ? 0 : ms > 0x7FFFFFFF ? 0x7FFFFFFF : (int)ms;
}

/* ── mutexes ──────────────────────────────────────────────────────── */

int pthread_mutexattr_init(pthread_mutexattr_t* a) { memset(a, 0, sizeof(*a)); return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t* a) { (void)a; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t* a, int t) { if (t < 0 || t > 2) return EINVAL; a->type = t; return 0; }
int pthread_mutexattr_gettype(const pthread_mutexattr_t* a, int* t) { *t = a->type; return 0; }
int pthread_mutexattr_setpshared(pthread_mutexattr_t* a, int p) { a->pshared = p; return 0; }
int pthread_mutexattr_getpshared(const pthread_mutexattr_t* a, int* p) { *p = a->pshared; return 0; }
int pthread_mutexattr_setprotocol(pthread_mutexattr_t* a, int p) { a->protocol = p; return 0; }
int pthread_mutexattr_setrobust(pthread_mutexattr_t* a, int r) { a->robust = r; return 0; }

int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* a) {
    memset(m, 0, sizeof(*m));
    m->type = a ? a->type : PTHREAD_MUTEX_NORMAL;
    return 0;
}
int pthread_mutex_destroy(pthread_mutex_t* m) { return m->state ? EBUSY : 0; }

/* the lock word: 0 free, 1 taken, 2 taken and someone may wait (Drepper's
 * "futexes are tricky", mutex 2); ms < 0: no time limit */
static int take(pthread_mutex_t* m, int timed, clockid_t clk, const struct timespec* abstime) {
    int c = CAS(&m->state, 0, 1);
    if (c == 0) return 0;
    if (c != 2) c = XCHG(&m->state, 2);
    while (c != 0) {
        int ms = -1;
        if (timed) {
            ms = ms_until(clk, abstime);
            if (!ms) return ETIMEDOUT;
        }
        fwait(&m->state, 2, ms);
        c = XCHG(&m->state, 2);
    }
    return 0;
}

static int lock_any(pthread_mutex_t* m, int timed, const struct timespec* abstime) {
    if (m->type != PTHREAD_MUTEX_NORMAL) {
        pthread_t me = pthread_self();
        if (m->owner == me) {
            if (m->type == PTHREAD_MUTEX_ERRORCHECK) return EDEADLK;
            m->count++;
            return 0;
        }
        int rc = take(m, timed, CLOCK_REALTIME, abstime);
        if (rc) return rc;
        m->owner = me;
        m->count = 1;
        return 0;
    }
    return take(m, timed, CLOCK_REALTIME, abstime);
}

int pthread_mutex_lock(pthread_mutex_t* m) { return lock_any(m, 0, NULL); }
int pthread_mutex_timedlock(pthread_mutex_t* m, const struct timespec* t) { return lock_any(m, 1, t); }

int pthread_mutex_trylock(pthread_mutex_t* m) {
    if (m->type != PTHREAD_MUTEX_NORMAL && m->owner == pthread_self()) {
        if (m->type == PTHREAD_MUTEX_ERRORCHECK) return EBUSY;
        m->count++;
        return 0;
    }
    if (CAS(&m->state, 0, 1) != 0) return EBUSY;
    if (m->type != PTHREAD_MUTEX_NORMAL) { m->owner = pthread_self(); m->count = 1; }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* m) {
    if (m->type != PTHREAD_MUTEX_NORMAL) {
        if (m->owner != pthread_self()) return EPERM;
        if (--m->count > 0) return 0;
        m->owner = NULL;
    }
    if (XADD(&m->state, -1) != 1) {      /* someone may wait */
        m->state = 0;
        fwake(&m->state, 1);
    }
    return 0;
}

/* ── condition variables ──────────────────────────────────────────── */

int pthread_condattr_init(pthread_condattr_t* a) { a->clock = CLOCK_REALTIME; a->pshared = 0; return 0; }
int pthread_condattr_destroy(pthread_condattr_t* a) { (void)a; return 0; }
int pthread_condattr_setclock(pthread_condattr_t* a, clockid_t c) {
    if (c != CLOCK_REALTIME && c != CLOCK_MONOTONIC) return EINVAL;
    a->clock = c;
    return 0;
}
int pthread_condattr_getclock(const pthread_condattr_t* a, clockid_t* c) { *c = a->clock; return 0; }
int pthread_condattr_setpshared(pthread_condattr_t* a, int p) { a->pshared = p; return 0; }

int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t* a) {
    c->seq = 0;
    c->clock = a ? a->clock : CLOCK_REALTIME;
    return 0;
}
int pthread_cond_destroy(pthread_cond_t* c) { (void)c; return 0; }

static int cond_wait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* abstime) {
    int seq = c->seq;
    /* (a recursive mutex is let go of entirely, and taken back as deep) */
    int depth = m->type == PTHREAD_MUTEX_RECURSIVE ? m->count : 1;
    if (m->type != PTHREAD_MUTEX_NORMAL) {
        if (m->owner != pthread_self()) return EPERM;
        m->count = 1;
    }
    pthread_mutex_unlock(m);
    int rc = 0;
    if (abstime) {
        int ms = ms_until(c->clock, abstime);
        if (!ms || fwait(&c->seq, seq, ms) == 1) rc = ETIMEDOUT;
    } else {
        fwait(&c->seq, seq, -1);
    }
    pthread_mutex_lock(m);
    if (m->type == PTHREAD_MUTEX_RECURSIVE) m->count = depth;
    return rc;
}

int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) { return cond_wait(c, m, NULL); }
int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* t) { return cond_wait(c, m, t); }
int pthread_cond_signal(pthread_cond_t* c) { XADD(&c->seq, 1); fwake(&c->seq, 1); return 0; }
int pthread_cond_broadcast(pthread_cond_t* c) { XADD(&c->seq, 1); fwake(&c->seq, 0); return 0; }

/* ── read-write locks ─────────────────────────────────────────────── */

int pthread_rwlockattr_init(pthread_rwlockattr_t* a) { a->pshared = 0; return 0; }
int pthread_rwlockattr_destroy(pthread_rwlockattr_t* a) { (void)a; return 0; }
int pthread_rwlock_init(pthread_rwlock_t* l, const pthread_rwlockattr_t* a) {
    (void)a;
    memset(l, 0, sizeof(*l));
    l->cond.clock = CLOCK_REALTIME;
    return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t* l) { return l->readers || l->writer ? EBUSY : 0; }

int pthread_rwlock_rdlock(pthread_rwlock_t* l) {
    pthread_mutex_lock(&l->lock);
    while (l->writer || l->waiting_writers) pthread_cond_wait(&l->cond, &l->lock);
    l->readers++;
    pthread_mutex_unlock(&l->lock);
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t* l) {
    pthread_mutex_lock(&l->lock);
    int ok = !l->writer && !l->waiting_writers;
    if (ok) l->readers++;
    pthread_mutex_unlock(&l->lock);
    return ok ? 0 : EBUSY;
}

int pthread_rwlock_wrlock(pthread_rwlock_t* l) {
    pthread_mutex_lock(&l->lock);
    l->waiting_writers++;
    while (l->writer || l->readers) pthread_cond_wait(&l->cond, &l->lock);
    l->waiting_writers--;
    l->writer = 1;
    pthread_mutex_unlock(&l->lock);
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t* l) {
    pthread_mutex_lock(&l->lock);
    int ok = !l->writer && !l->readers;
    if (ok) l->writer = 1;
    pthread_mutex_unlock(&l->lock);
    return ok ? 0 : EBUSY;
}

int pthread_rwlock_unlock(pthread_rwlock_t* l) {
    pthread_mutex_lock(&l->lock);
    if (l->writer) l->writer = 0;
    else if (l->readers) l->readers--;
    pthread_cond_broadcast(&l->cond);
    pthread_mutex_unlock(&l->lock);
    return 0;
}

/* ── once ─────────────────────────────────────────────────────────── */

int pthread_once(pthread_once_t* o, void (*fn)(void)) {
    if (*o == 2) return 0;
    if (CAS(o, 0, 1) == 0) {
        fn();
        *o = 2;
        fwake(o, 0);
        return 0;
    }
    while (*o == 1) fwait(o, 1, -1);
    return 0;
}

/* ── spinlocks, barriers ──────────────────────────────────────────── */

int pthread_spin_init(pthread_spinlock_t* s, int pshared) { (void)pshared; *s = 0; return 0; }
int pthread_spin_destroy(pthread_spinlock_t* s) { (void)s; return 0; }
int pthread_spin_trylock(pthread_spinlock_t* s) { return XCHG(s, 1) ? EBUSY : 0; }
int pthread_spin_unlock(pthread_spinlock_t* s) { *s = 0; return 0; }
int pthread_spin_lock(pthread_spinlock_t* s) {
    int spins = 0;
    while (XCHG(s, 1)) {
        while (*s) {
            if (++spins > 200) { __banana->yield(); spins = 0; }
            __asm__ volatile("pause");
        }
    }
    return 0;
}

int pthread_barrier_init(pthread_barrier_t* b, const pthread_barrierattr_t* a, unsigned count) {
    (void)a;
    if (!count) return EINVAL;
    memset(b, 0, sizeof(*b));
    b->cond.clock = CLOCK_REALTIME;
    b->count = count;
    return 0;
}
int pthread_barrier_destroy(pthread_barrier_t* b) { (void)b; return 0; }

int pthread_barrier_wait(pthread_barrier_t* b) {
    pthread_mutex_lock(&b->lock);
    unsigned round = b->round;
    if (++b->waiting == b->count) {
        b->waiting = 0;
        b->round++;
        pthread_cond_broadcast(&b->cond);
        pthread_mutex_unlock(&b->lock);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (round == b->round) pthread_cond_wait(&b->cond, &b->lock);
    pthread_mutex_unlock(&b->lock);
    return 0;
}

/* ── semaphores ───────────────────────────────────────────────────── */

int sem_init(sem_t* s, int pshared, unsigned value) { (void)pshared; s->value = (int)value; s->waiters = 0; return 0; }
int sem_destroy(sem_t* s) { (void)s; return 0; }

int sem_trywait(sem_t* s) {
    for (;;) {
        int v = s->value;
        if (v <= 0) { errno = EAGAIN; return -1; }
        if (CAS(&s->value, v, v - 1) == v) return 0;
    }
}

static int sem_take(sem_t* s, const struct timespec* abstime) {
    for (;;) {
        int v = s->value;
        if (v > 0) {
            if (CAS(&s->value, v, v - 1) == v) return 0;
            continue;
        }
        int ms = -1;
        if (abstime) {
            ms = ms_until(CLOCK_REALTIME, abstime);
            if (!ms) { errno = ETIMEDOUT; return -1; }
        }
        XADD(&s->waiters, 1);
        fwait(&s->value, v, ms);
        XADD(&s->waiters, -1);
    }
}

int sem_wait(sem_t* s) { return sem_take(s, NULL); }
int sem_timedwait(sem_t* s, const struct timespec* t) { return sem_take(s, t); }
int sem_post(sem_t* s) {
    XADD(&s->value, 1);
    if (s->waiters) fwake(&s->value, 1);
    return 0;
}
int sem_getvalue(sem_t* s, int* v) { *v = s->value; return 0; }

/* ── sched ────────────────────────────────────────────────────────── */

int sched_get_priority_min(int policy) { (void)policy; return 0; }
int sched_get_priority_max(int policy) { (void)policy; return 0; }
int sched_getcpu(void) { return 0; }

int __sched_cpucount(size_t size, const cpu_set_t* s) {
    int n = 0;
    const unsigned char* b = (const unsigned char*)s;
    for (size_t i = 0; i < size; i++) for (unsigned v = b[i]; v; v &= v - 1) n++;
    return n;
}

int sched_getaffinity(pid_t pid, size_t size, cpu_set_t* s) {
    (void)pid;
    memset(s, 0, size);
    int n = __banana->version >= 3 ? __banana->cpu_count() : 1;
    for (int c = 0; c < n && (size_t)c < size * 8; c++) ((unsigned char*)s)[c / 8] |= (unsigned char)(1u << (c % 8));
    return 0;
}

int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t* s) { (void)pid; (void)size; (void)s; return 0; }
