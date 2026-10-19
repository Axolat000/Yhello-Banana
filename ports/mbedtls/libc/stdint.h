/* Freestanding libc declarations for the Banana Mbed TLS port. */
#ifndef SHIM_STDINT_H
#define SHIM_STDINT_H
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef __UINTPTR_TYPE__   uintptr_t;
#define UINT8_MAX 255u
#define UINT16_MAX 65535u
#define UINT32_MAX 4294967295u
#define INT32_MAX 2147483647
#define INT32_MIN (-INT32_MAX - 1)
#define UINT64_MAX 18446744073709551615ull
#define INT64_MAX 9223372036854775807ll
#define INT64_MIN (-INT64_MAX - 1ll)
#define SIZE_MAX __SIZE_MAX__
#define UINT64_C(n) n##ull
#define INT64_C(n) n##ll
#endif
