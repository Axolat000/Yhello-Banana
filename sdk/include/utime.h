/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _UTIME_H
#define _UTIME_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <time.h>
struct utimbuf { time_t actime, modtime; };
int utime(const char* path, const struct utimbuf* times);     /* (modtime; NULL: now) */
#ifdef __cplusplus
}
#endif
#endif
