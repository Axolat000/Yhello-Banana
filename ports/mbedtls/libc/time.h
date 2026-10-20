#ifndef BANANA_TLS_TIME_H
#define BANANA_TLS_TIME_H
#include "types.h"
typedef int64_t time_t;
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};
#endif
