/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS SDK: the little C++ runtime that code built with g++
 * -fno-exceptions -fno-rtti needs (HotSpot, ports): operator new / delete,
 * one-time initialisation of local statics, destructors at exit, pure
 * virtual calls. Written in C under the C++ (Itanium ABI) names. */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* ── operator new / delete ────────────────────────────────────────── */

/* (size_t in the mangled names: unsigned long "m" on x86_64, unsigned int "j" on i686) */
#ifdef __x86_64__
#define SZ "m"
#else
#define SZ "j"
#endif

static void* must(size_t n) {
    void* p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "operator new: out of memory (%lu bytes)\n", (unsigned long)n); abort(); }
    return p;
}

static void* aligned(size_t n, size_t a, int nothrow) {
    void* p = NULL;
    if (posix_memalign(&p, a < sizeof(void*) ? sizeof(void*) : a, n ? n : 1)) p = NULL;
    if (!p && !nothrow) { fprintf(stderr, "operator new: out of memory\n"); abort(); }
    return p;
}

void* cxx_new(size_t n)                      __asm__("_Znw" SZ);
void* cxx_new_arr(size_t n)                  __asm__("_Zna" SZ);
void* cxx_new_nt(size_t n, const void* t)    __asm__("_Znw" SZ "RKSt9nothrow_t");
void* cxx_new_arr_nt(size_t n, const void* t) __asm__("_Zna" SZ "RKSt9nothrow_t");
void* cxx_new_al(size_t n, size_t a)         __asm__("_Znw" SZ "St11align_val_t");
void* cxx_new_arr_al(size_t n, size_t a)     __asm__("_Zna" SZ "St11align_val_t");
void  cxx_del(void* p)                       __asm__("_ZdlPv");
void  cxx_del_arr(void* p)                   __asm__("_ZdaPv");
void  cxx_del_sz(void* p, size_t n)          __asm__("_ZdlPv" SZ);
void  cxx_del_arr_sz(void* p, size_t n)      __asm__("_ZdaPv" SZ);
void  cxx_del_nt(void* p, const void* t)     __asm__("_ZdlPvRKSt9nothrow_t");
void  cxx_del_arr_nt(void* p, const void* t) __asm__("_ZdaPvRKSt9nothrow_t");
void  cxx_del_al(void* p, size_t a)          __asm__("_ZdlPvSt11align_val_t");
void  cxx_del_arr_al(void* p, size_t a)      __asm__("_ZdaPvSt11align_val_t");
void  cxx_del_sz_al(void* p, size_t n, size_t a)     __asm__("_ZdlPv" SZ "St11align_val_t");
void  cxx_del_arr_sz_al(void* p, size_t n, size_t a) __asm__("_ZdaPv" SZ "St11align_val_t");

void* cxx_new(size_t n) { return must(n); }
void* cxx_new_arr(size_t n) { return must(n); }
void* cxx_new_nt(size_t n, const void* t) { (void)t; return malloc(n ? n : 1); }
void* cxx_new_arr_nt(size_t n, const void* t) { (void)t; return malloc(n ? n : 1); }
void* cxx_new_al(size_t n, size_t a) { return aligned(n, a, 0); }
void* cxx_new_arr_al(size_t n, size_t a) { return aligned(n, a, 0); }
void  cxx_del(void* p) { free(p); }
void  cxx_del_arr(void* p) { free(p); }
void  cxx_del_sz(void* p, size_t n) { (void)n; free(p); }
void  cxx_del_arr_sz(void* p, size_t n) { (void)n; free(p); }
void  cxx_del_nt(void* p, const void* t) { (void)t; free(p); }
void  cxx_del_arr_nt(void* p, const void* t) { (void)t; free(p); }
void  cxx_del_al(void* p, size_t a) { (void)a; free(p); }
void  cxx_del_arr_al(void* p, size_t a) { (void)a; free(p); }
void  cxx_del_sz_al(void* p, size_t n, size_t a) { (void)n; (void)a; free(p); }
void  cxx_del_arr_sz_al(void* p, size_t n, size_t a) { (void)n; (void)a; free(p); }

/* std::nothrow */
const char cxx_nothrow __asm__("_ZSt7nothrow") = 0;

/* ── statics, exit, pure virtuals ─────────────────────────────────── */

/* a local static's guard: byte 0 set once it is initialised; one thread
 * initialises it, the others wait */
static pthread_mutex_t g_guard = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

int __cxa_guard_acquire(long long* g) {
    if (*(volatile char*)g) return 0;
    pthread_mutex_lock(&g_guard);
    if (*(volatile char*)g) { pthread_mutex_unlock(&g_guard); return 0; }
    return 1;                                /* (the caller initialises, then releases) */
}

void __cxa_guard_release(long long* g) {
    *(volatile char*)g = 1;
    pthread_mutex_unlock(&g_guard);
}

void __cxa_guard_abort(long long* g) { (void)g; pthread_mutex_unlock(&g_guard); }

/* the objects every library shares (no per-library crtbegin here) */
void* __dso_handle = &__dso_handle;

typedef struct dtor { void (*fn)(void*); void* arg; void* dso; struct dtor* next; } dtor_t;
static dtor_t* g_dtors;
static pthread_mutex_t g_dtor_lock = PTHREAD_MUTEX_INITIALIZER;

void __cxa_finalize(void* dso) {
    for (;;) {
        pthread_mutex_lock(&g_dtor_lock);
        dtor_t** pp = &g_dtors;
        while (*pp && dso && (*pp)->dso != dso) pp = &(*pp)->next;
        dtor_t* d = *pp;
        if (d) *pp = d->next;
        pthread_mutex_unlock(&g_dtor_lock);
        if (!d) return;
        d->fn(d->arg);
        free(d);
    }
}

static void run_all(void) { __cxa_finalize(NULL); }

int __cxa_atexit(void (*fn)(void*), void* arg, void* dso) {
    static int registered;
    dtor_t* d = malloc(sizeof(*d));
    if (!d) return -1;
    d->fn = fn;
    d->arg = arg;
    d->dso = dso;
    pthread_mutex_lock(&g_dtor_lock);
    d->next = g_dtors;                       /* (newest first: run in reverse order) */
    g_dtors = d;
    int first = !registered;
    registered = 1;
    pthread_mutex_unlock(&g_dtor_lock);
    if (first) atexit(run_all);
    return 0;
}

int __cxa_thread_atexit(void (*fn)(void*), void* arg, void* dso) { return __cxa_atexit(fn, arg, dso); }

void __cxa_pure_virtual(void) {
    fprintf(stderr, "pure virtual method called\n");
    abort();
}

void __cxa_deleted_virtual(void) {
    fprintf(stderr, "deleted virtual method called\n");
    abort();
}

/* abi::__cxa_demangle: no demangler here (status -2: "not a valid name"),
 * callers fall back to the mangled name */
char* __cxa_demangle(const char* name, char* buf, size_t* len, int* status) {
    (void)name; (void)buf; (void)len;
    if (status) *status = -2;
    return NULL;
}

/* code built with -fstack-protector (prebuilt static libraries) */
void __stack_chk_fail(void) {
    fprintf(stderr, "stack smashing detected\n");
    abort();
}

/* and with -D_FORTIFY_SOURCE (Ubuntu's default: its libffi.a, linked into
 * OpenJDK's libjvm.so): glibc's checked copies, dst_len being the size of
 * the destination the compiler could see */
static void chk_fail(const char* what) {
    fprintf(stderr, "%s: buffer overflow detected\n", what);
    abort();
}
void* __memcpy_chk(void* dst, const void* src, size_t n, size_t dst_len) {
    if (n > dst_len) chk_fail("memcpy");
    return memcpy(dst, src, n);
}
void* __memmove_chk(void* dst, const void* src, size_t n, size_t dst_len) {
    if (n > dst_len) chk_fail("memmove");
    return memmove(dst, src, n);
}
void* __memset_chk(void* dst, int c, size_t n, size_t dst_len) {
    if (n > dst_len) chk_fail("memset");
    return memset(dst, c, n);
}

/* the virtual tables of std::type_info's subclasses, which every class
 * with virtual functions points at (RTTI): typeid and dynamic_cast are
 * not supported, so they only need to exist */
const void* const cxx_class_ti_vt[8]     __asm__("_ZTVN10__cxxabiv117__class_type_infoE") = { 0 };
const void* const cxx_si_class_ti_vt[8]  __asm__("_ZTVN10__cxxabiv120__si_class_type_infoE") = { 0 };
const void* const cxx_vmi_class_ti_vt[8] __asm__("_ZTVN10__cxxabiv121__vmi_class_type_infoE") = { 0 };
const void* const cxx_fund_ti_vt[8]      __asm__("_ZTVN10__cxxabiv123__fundamental_type_infoE") = { 0 };
const void* const cxx_pointer_ti_vt[8]   __asm__("_ZTVN10__cxxabiv119__pointer_type_infoE") = { 0 };

/* (libstdc++'s headers call these where they would throw) */
void cxx_throw_system_error(int e)       __asm__("_ZSt20__throw_system_errori");
void cxx_throw_bad_array_new(void)       __asm__("_ZSt28__throw_bad_array_new_lengthv");
void cxx_throw_bad_function_call(void)   __asm__("_ZSt25__throw_bad_function_callv");
void cxx_throw_invalid_argument(const char*) __asm__("_ZSt24__throw_invalid_argumentPKc");
void cxx_throw_system_error(int e) { fprintf(stderr, "system_error: %d\n", e); abort(); }
void cxx_throw_bad_array_new(void) { fprintf(stderr, "bad_array_new_length\n"); abort(); }
void cxx_throw_bad_function_call(void) { fprintf(stderr, "bad_function_call\n"); abort(); }
void cxx_throw_invalid_argument(const char* s) { fprintf(stderr, "invalid_argument: %s\n", s); abort(); }
void cxx_throw_bad_alloc(void)          __asm__("_ZSt17__throw_bad_allocv");
void cxx_throw_length_error(const char*) __asm__("_ZSt20__throw_length_errorPKc");
void cxx_throw_out_of_range(const char*) __asm__("_ZSt20__throw_out_of_rangePKc");
void cxx_throw_logic_error(const char*) __asm__("_ZSt19__throw_logic_errorPKc");
void cxx_throw_bad_alloc(void) { fprintf(stderr, "bad_alloc\n"); abort(); }
void cxx_throw_length_error(const char* s) { fprintf(stderr, "length_error: %s\n", s); abort(); }
void cxx_throw_out_of_range(const char* s) { fprintf(stderr, "out_of_range: %s\n", s); abort(); }
void cxx_throw_logic_error(const char* s) { fprintf(stderr, "logic_error: %s\n", s); abort(); }
