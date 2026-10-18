#ifndef TIMER_MATH_H
#define TIMER_MATH_H
#include "types.h"

/* Keep the multiply below 2^64 even after days of uptime. */
static inline uint64_t timer_hpet_ns(uint64_t ticks, uint32_t period_fs) {
    return ticks / 1000000u * period_fs + ticks % 1000000u * period_fs / 1000000u;
}
static inline uint64_t timer_pm_ns(uint64_t ticks) {
    return ticks / 3579545u * 1000000000u + ticks % 3579545u * 1000000000u / 3579545u;
}
static inline uint32_t timer_wrap_delta(uint32_t now, uint32_t before, uint32_t mask) {
    return (now - before) & mask;
}
void timer_math_tests(void (*report)(const char*, int, const char*));
#endif
