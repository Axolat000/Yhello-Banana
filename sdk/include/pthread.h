#ifndef _PTHREAD_H
#define _PTHREAD_H
#ifdef __cplusplus
extern "C" {
#endif
/*
 * POSIX threads for Banana OS apps (API version 17): threads with a stack
 * of the size asked for (mapped, a guard page under it: on the 64-bit
 * system running off its end stops the app), joinable or detached;
 * mutexes (normal, recursive, error-checking), condition variables (on
 * CLOCK_REALTIME or CLOCK_MONOTONIC), read-write locks, once, keys,
 * spinlocks, barriers. Blocking waits sleep in the kernel (wait / wake).
 *
 * __thread variables work on the 64-bit system for every thread started
 * here (or by banana_thread) and the main thread; the 32-bit system has no
 * thread pointer: use keys there. Up to 64 threads besides the main one.
 */
#include <stddef.h>
#include <sched.h>
#include <time.h>

typedef struct __pthread* pthread_t;

typedef struct {
    int    detached;
    size_t stack_size;            /* 0: the default (1 MiB) */
    size_t guard_size;
    void*  stack;                 /* pthread_attr_setstack: the app's own */
    int    sched_policy, sched_priority, inherit, scope;
} pthread_attr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_INHERIT_SCHED   0
#define PTHREAD_EXPLICIT_SCHED  1
#define PTHREAD_SCOPE_SYSTEM    0
#define PTHREAD_SCOPE_PROCESS   1
#define PTHREAD_STACK_MIN       16384

typedef struct {
    volatile int state;           /* 0 free, 1 locked, 2 locked and someone waits */
    int          type;
    pthread_t    owner;
    int          count;           /* (recursive) */
} pthread_mutex_t;

typedef struct { int type; int pshared; int robust; int protocol; } pthread_mutexattr_t;

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL
#define PTHREAD_PROCESS_PRIVATE  0
#define PTHREAD_PROCESS_SHARED   1
#define PTHREAD_PRIO_NONE        0
#define PTHREAD_MUTEX_STALLED    0
#define PTHREAD_MUTEX_ROBUST     1
#define PTHREAD_MUTEX_INITIALIZER { 0, PTHREAD_MUTEX_NORMAL, 0, 0 }
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP { 0, PTHREAD_MUTEX_RECURSIVE, 0, 0 }
#define PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP { 0, PTHREAD_MUTEX_ERRORCHECK, 0, 0 }

typedef struct {
    volatile int seq;             /* changes at every signal / broadcast */
    int          clock;
} pthread_cond_t;
typedef struct { int clock; int pshared; } pthread_condattr_t;
#define PTHREAD_COND_INITIALIZER { 0, CLOCK_REALTIME }

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    int             readers, writer, waiting_writers;
} pthread_rwlock_t;
typedef struct { int pshared; } pthread_rwlockattr_t;
#define PTHREAD_RWLOCK_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0 }

typedef volatile int pthread_once_t;
#define PTHREAD_ONCE_INIT 0

typedef unsigned int pthread_key_t;
#define PTHREAD_KEYS_MAX 128
#define PTHREAD_DESTRUCTOR_ITERATIONS 4

typedef volatile int pthread_spinlock_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    unsigned        count, waiting, round;
} pthread_barrier_t;
typedef struct { int pshared; } pthread_barrierattr_t;
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

#define PTHREAD_CANCEL_ENABLE        0
#define PTHREAD_CANCEL_DISABLE       1
#define PTHREAD_CANCEL_DEFERRED      0
#define PTHREAD_CANCEL_ASYNCHRONOUS  1
#define PTHREAD_CANCELED             ((void*)-1)

/* threads */
int       pthread_create(pthread_t* t, const pthread_attr_t* attr, void* (*fn)(void*), void* arg);
int       pthread_join(pthread_t t, void** ret);
int       pthread_detach(pthread_t t);
void      pthread_exit(void* ret) __attribute__((noreturn));
pthread_t pthread_self(void);
int       pthread_equal(pthread_t a, pthread_t b);
int       pthread_yield(void);
int       pthread_setname_np(pthread_t t, const char* name);
int       pthread_getname_np(pthread_t t, char* name, size_t len);
int       pthread_getattr_np(pthread_t t, pthread_attr_t* attr);   /* its stack, for stack-checking runtimes */
int       pthread_getcpuclockid(pthread_t t, clockid_t* clk);
int       pthread_setschedparam(pthread_t t, int policy, const struct sched_param* p);
int       pthread_getschedparam(pthread_t t, int* policy, struct sched_param* p);
int       pthread_setcancelstate(int state, int* old);
int       pthread_setcanceltype(int type, int* old);
void      pthread_testcancel(void);
int       pthread_cancel(pthread_t t);              /* (not supported: ENOSYS) */
int       pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));

int pthread_attr_init(pthread_attr_t* a);
int pthread_attr_destroy(pthread_attr_t* a);
int pthread_attr_setdetachstate(pthread_attr_t* a, int s);
int pthread_attr_getdetachstate(const pthread_attr_t* a, int* s);
int pthread_attr_setstacksize(pthread_attr_t* a, size_t n);
int pthread_attr_getstacksize(const pthread_attr_t* a, size_t* n);
int pthread_attr_setguardsize(pthread_attr_t* a, size_t n);
int pthread_attr_getguardsize(const pthread_attr_t* a, size_t* n);
int pthread_attr_setstack(pthread_attr_t* a, void* stack, size_t n);
int pthread_attr_getstack(const pthread_attr_t* a, void** stack, size_t* n);
int pthread_attr_setschedpolicy(pthread_attr_t* a, int p);
int pthread_attr_getschedpolicy(const pthread_attr_t* a, int* p);
int pthread_attr_setschedparam(pthread_attr_t* a, const struct sched_param* p);
int pthread_attr_getschedparam(const pthread_attr_t* a, struct sched_param* p);
int pthread_attr_setinheritsched(pthread_attr_t* a, int i);
int pthread_attr_getinheritsched(const pthread_attr_t* a, int* i);
int pthread_attr_setscope(pthread_attr_t* a, int s);
int pthread_attr_getscope(const pthread_attr_t* a, int* s);

/* mutexes */
int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* a);
int pthread_mutex_destroy(pthread_mutex_t* m);
int pthread_mutex_lock(pthread_mutex_t* m);
int pthread_mutex_trylock(pthread_mutex_t* m);
int pthread_mutex_timedlock(pthread_mutex_t* m, const struct timespec* abstime);
int pthread_mutex_clocklock(pthread_mutex_t* m, clockid_t clk, const struct timespec* abstime);
int pthread_getaffinity_np(pthread_t t, size_t size, cpu_set_t* s);
int pthread_setaffinity_np(pthread_t t, size_t size, const cpu_set_t* s);
int pthread_mutex_unlock(pthread_mutex_t* m);
int pthread_mutexattr_init(pthread_mutexattr_t* a);
int pthread_mutexattr_destroy(pthread_mutexattr_t* a);
int pthread_mutexattr_settype(pthread_mutexattr_t* a, int t);
int pthread_mutexattr_gettype(const pthread_mutexattr_t* a, int* t);
int pthread_mutexattr_setpshared(pthread_mutexattr_t* a, int p);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t* a, int* p);
int pthread_mutexattr_setprotocol(pthread_mutexattr_t* a, int p);
int pthread_mutexattr_setrobust(pthread_mutexattr_t* a, int r);

/* condition variables */
int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t* a);
int pthread_cond_destroy(pthread_cond_t* c);
int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m);
int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* abstime);
int pthread_cond_clockwait(pthread_cond_t* c, pthread_mutex_t* m, clockid_t clk, const struct timespec* abstime);
int pthread_cond_signal(pthread_cond_t* c);
int pthread_cond_broadcast(pthread_cond_t* c);
int pthread_condattr_init(pthread_condattr_t* a);
int pthread_condattr_destroy(pthread_condattr_t* a);
int pthread_condattr_setclock(pthread_condattr_t* a, clockid_t clk);
int pthread_condattr_getclock(const pthread_condattr_t* a, clockid_t* clk);
int pthread_condattr_setpshared(pthread_condattr_t* a, int p);

/* read-write locks */
int pthread_rwlock_init(pthread_rwlock_t* l, const pthread_rwlockattr_t* a);
int pthread_rwlock_destroy(pthread_rwlock_t* l);
int pthread_rwlock_rdlock(pthread_rwlock_t* l);
int pthread_rwlock_tryrdlock(pthread_rwlock_t* l);
int pthread_rwlock_wrlock(pthread_rwlock_t* l);
int pthread_rwlock_trywrlock(pthread_rwlock_t* l);
int pthread_rwlock_unlock(pthread_rwlock_t* l);
int pthread_rwlockattr_init(pthread_rwlockattr_t* a);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t* a);

/* once, keys */
int   pthread_once(pthread_once_t* o, void (*fn)(void));
int   pthread_key_create(pthread_key_t* k, void (*destructor)(void*));
int   pthread_key_delete(pthread_key_t k);
void* pthread_getspecific(pthread_key_t k);
int   pthread_setspecific(pthread_key_t k, const void* v);

/* spinlocks, barriers */
int pthread_spin_init(pthread_spinlock_t* s, int pshared);
int pthread_spin_destroy(pthread_spinlock_t* s);
int pthread_spin_lock(pthread_spinlock_t* s);
int pthread_spin_trylock(pthread_spinlock_t* s);
int pthread_spin_unlock(pthread_spinlock_t* s);
int pthread_barrier_init(pthread_barrier_t* b, const pthread_barrierattr_t* a, unsigned count);
int pthread_barrier_destroy(pthread_barrier_t* b);
int pthread_barrier_wait(pthread_barrier_t* b);

/* cleanup handlers (run by pthread_exit, or by pop(1)) */
struct __pthread_cleanup { void (*fn)(void*); void* arg; struct __pthread_cleanup* next; };
void __pthread_cleanup_push(struct __pthread_cleanup* c, void (*fn)(void*), void* arg);
void __pthread_cleanup_pop(struct __pthread_cleanup* c, int run);
#define pthread_cleanup_push(fn, arg) { struct __pthread_cleanup __cu; __pthread_cleanup_push(&__cu, (fn), (arg));
#define pthread_cleanup_pop(run) __pthread_cleanup_pop(&__cu, (run)); }

#ifdef __cplusplus
}
#endif
#endif
