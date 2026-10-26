/* Freestanding libc declarations for the Banana Mbed TLS port. */
#ifndef SHIM_STDDEF_H
#define SHIM_STDDEF_H
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#ifndef NULL
#define NULL ((void*)0)
#endif
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
