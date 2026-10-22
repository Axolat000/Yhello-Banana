/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#ifdef __cplusplus
extern "C" {
#endif
#include <time.h>
struct timeval { long tv_sec; long tv_usec; };
int gettimeofday(struct timeval* tv, void* tz);
int utimes(const char* path, const struct timeval times[2]);
int futimes(int fd, const struct timeval times[2]);
int lutimes(const char* path, const struct timeval times[2]);
int futimesat(int dirfd, const char* path, const struct timeval times[2]);
/* interval timers: none run here (getitimer: disarmed; setitimer: ENOSYS but to disarm) */
struct itimerval { struct timeval it_interval, it_value; };
#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2
int getitimer(int which, struct itimerval* cur);
int setitimer(int which, const struct itimerval* val, struct itimerval* old);
#ifdef __cplusplus
}
#endif
#endif
