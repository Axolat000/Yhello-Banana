/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _STDIO_EXT_H
#define _STDIO_EXT_H
#ifdef __cplusplus
extern "C" {
#endif
/* looks into a stream (glibc's and musl's: gnulib uses them) */
#include <stdio.h>
#define FSETLOCKING_QUERY    0
#define FSETLOCKING_INTERNAL 1
#define FSETLOCKING_BYCALLER 2
size_t      __fpending(FILE* f);         /* bytes written, not yet given to the system */
int         __freading(FILE* f);
int         __fwriting(FILE* f);
int         __freadable(FILE* f);
int         __fwritable(FILE* f);
int         __flbf(FILE* f);
size_t      __fbufsize(FILE* f);
void        __fpurge(FILE* f);
int         __fsetlocking(FILE* f, int type);
void        _flushlbf(void);
size_t      __freadahead(FILE* f);       /* bytes read ahead, not yet taken */
const char* __freadptr(FILE* f, size_t* size);
void        __freadptrinc(FILE* f, size_t inc);
void        __fseterr(FILE* f);
#ifdef __cplusplus
}
#endif
#endif
