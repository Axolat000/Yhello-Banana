/* Freestanding libc declarations for the Banana Mbed TLS port. */
#ifndef SHIM_LIMITS_H
#define SHIM_LIMITS_H
#define CHAR_BIT  8
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define SHRT_MAX  32767
#define SHRT_MIN  (-32768)
#define USHRT_MAX 65535
#define INT_MAX   2147483647
#define INT_MIN   (-INT_MAX - 1)
#define UINT_MAX  4294967295u
#define LONG_MAX  __LONG_MAX__
#define LONG_MIN  (-LONG_MAX - 1L)
#define ULONG_MAX (2ul * __LONG_MAX__ + 1ul)
#endif
