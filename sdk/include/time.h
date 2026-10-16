#ifndef _TIME_H
#define _TIME_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long        tm_gmtoff;                  /* seconds east of UTC (0: the clock chip keeps local time) */
    const char* tm_zone;
};
char*      ctime(const time_t* t);
char*      ctime_r(const time_t* t, char* buf);
char*      asctime(const struct tm* tm);
char*      asctime_r(const struct tm* tm, char* buf);
double     difftime(time_t a, time_t b);
time_t     timegm(struct tm* tm);
void       tzset(void);
extern long  timezone;
extern int   daylight;
extern char* tzname[2];
time_t     time(time_t* t);                 /* seconds since 1970, from the clock chip */
clock_t    clock(void);                     /* milliseconds since the app started */
struct tm* localtime(const time_t* t);
struct tm* gmtime(const time_t* t);
struct tm* localtime_r(const time_t* t, struct tm* out);
struct tm* gmtime_r(const time_t* t, struct tm* out);
time_t     mktime(struct tm* tm);
size_t     strftime(char* buf, size_t max, const char* fmt, const struct tm* tm);

/* POSIX clocks, with the system timer's millisecond resolution */
#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec { time_t tv_sec; long tv_nsec; };
#endif
typedef int clockid_t;
#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2     /* (the time since the app started) */
#define CLOCK_THREAD_CPUTIME_ID  3     /* (likewise) */
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_REALTIME_COARSE    5
#define CLOCK_MONOTONIC_COARSE   6
#define CLOCK_BOOTTIME           7
#define TIMER_ABSTIME            1
int clock_gettime(clockid_t clk, struct timespec* ts);
int clock_getres(clockid_t clk, struct timespec* ts);
int clock_nanosleep(clockid_t clk, int flags, const struct timespec* req, struct timespec* rem);
int nanosleep(const struct timespec* req, struct timespec* rem);
#ifdef __cplusplus
}
#endif
#endif
