#ifndef _STDDEF_H
#define _STDDEF_H
#ifdef __cplusplus
extern "C" {
#endif
typedef __SIZE_TYPE__    size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#ifndef __cplusplus
typedef __WCHAR_TYPE__   wchar_t;     /* (a built-in type in C++) */
#endif
#ifndef NULL
#ifdef __cplusplus
#define NULL __null
#else
#define NULL ((void*)0)
#endif
#endif
#define offsetof(t, m) __builtin_offsetof(t, m)
typedef struct { long long __ll __attribute__((aligned(__alignof__(long long)))); long double __ld __attribute__((aligned(__alignof__(long double)))); } max_align_t;
#ifdef __cplusplus
}
#endif
#endif
