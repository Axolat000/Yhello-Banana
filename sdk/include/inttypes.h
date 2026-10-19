/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _INTTYPES_H
#define _INTTYPES_H
#ifdef __cplusplus
extern "C" {
#endif
/* Banana OS SDK: printf / scanf formats of the fixed-width types */
#include <stdint.h>
#if __SIZEOF_LONG__ == 8
#define __PRI64 "l"
#else
#define __PRI64 "ll"
#endif
#define PRId8  "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 __PRI64 "d"
#define PRIi32 "i"
#define PRIi64 __PRI64 "i"
#define PRIu8  "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 __PRI64 "u"
#define PRIx8  "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 __PRI64 "x"
#define PRIX32 "X"
#define PRIX64 __PRI64 "X"
#define PRIo64 __PRI64 "o"
#define SCNd64 __PRI64 "d"
#define SCNu64 __PRI64 "u"
#define SCNx64 __PRI64 "x"
#define SCNd32 "d"
#define SCNu32 "u"
#define SCNx32 "x"
#if __SIZEOF_POINTER__ == 8
#define PRIdPTR "ld"
#define PRIxPTR "lx"
#define PRIuPTR "lu"
#else
#define PRIdPTR "d"
#define PRIxPTR "x"
#define PRIuPTR "u"
#endif
#define PRIdMAX __PRI64 "d"
#define PRIuMAX __PRI64 "u"
#define PRIiMAX __PRI64 "i"
#define PRIxMAX __PRI64 "x"
#define PRIXMAX __PRI64 "X"
#define PRIoMAX __PRI64 "o"
#define PRIi8  "i"
#define PRIi16 "i"
#define PRIo8  "o"
#define PRIo16 "o"
#define PRIo32 "o"
#define PRIX8  "X"
#define PRIX16 "X"
#define PRIiPTR PRIdPTR
#define PRIXPTR PRIxPTR
#define SCNi32 "i"
#define SCNi64 __PRI64 "i"
#define SCNo32 "o"
#define SCNo64 __PRI64 "o"
#define SCNd8  "hhd"
#define SCNu8  "hhu"
#define SCNx8  "hhx"
#define SCNd16 "hd"
#define SCNu16 "hu"
#define SCNx16 "hx"
#define SCNdMAX __PRI64 "d"
#define SCNuMAX __PRI64 "u"
#define SCNxMAX __PRI64 "x"
typedef struct { intmax_t quot, rem; } imaxdiv_t;
intmax_t imaxabs(intmax_t x);
imaxdiv_t imaxdiv(intmax_t a, intmax_t b);
intmax_t strtoimax(const char* s, char** end, int base);
uintmax_t strtoumax(const char* s, char** end, int base);
#ifdef __cplusplus
}
#endif
#endif
