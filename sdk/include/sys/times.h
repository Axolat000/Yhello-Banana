/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <time.h>
struct tms { clock_t tms_utime, tms_stime, tms_cutime, tms_cstime; };
clock_t times(struct tms* t);       /* ticks of sysconf(_SC_CLK_TCK) */
#ifdef __cplusplus
}
#endif
#endif
