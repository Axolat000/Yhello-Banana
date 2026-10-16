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
#ifdef __cplusplus
}
#endif
#endif
