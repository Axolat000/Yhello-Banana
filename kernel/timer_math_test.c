/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "timer_math.h"
void timer_math_tests(void (*report)(const char*, int, const char*)) {
#define CHECK(label, expression) report(label, !!(expression), "")
    CHECK("HPET converts zero and fractional nanoseconds", timer_hpet_ns(0,69841279)==0 && timer_hpet_ns(1,69841279)==69);
    CHECK("HPET converts a full second", timer_hpet_ns(100000000,10000000)==1000000000ull);
    CHECK("HPET retains fractions across a long interval", timer_hpet_ns(123456789,69841279)==8622380044ull);
    CHECK("HPET conversion survives 30 days without multiply overflow", timer_hpet_ns(259200000000000ull,10000000)==2592000000000000ull);
    CHECK("PM timer converts a full second", timer_pm_ns(3579545)==1000000000ull);
    CHECK("PM timer retains submillisecond precision", timer_pm_ns(3579)==999847ull);
    CHECK("PM timer conversion survives 30 days", timer_pm_ns(9278180640000ull)==2592000000000000ull);
    CHECK("24-bit timer extends across rollover", timer_wrap_delta(0x10,0xfffff0,0xffffff)==32);
    CHECK("32-bit timer extends across rollover", timer_wrap_delta(0x10,0xfffffff0,0xffffffff)==32);
    CHECK("timer rollover masks unused high bits", timer_wrap_delta(0xff000010,0xfffff0,0xffffff)==32);
    CHECK("unchanged counters do not advance time", !timer_wrap_delta(123,123,0xffffff));
    CHECK("elapsed time composes across repeated rollovers", (uint64_t)timer_wrap_delta(3,0xfffffa,0xffffff)+timer_wrap_delta(0xfffffc,3,0xffffff)+timer_wrap_delta(4,0xfffffc,0xffffff)==16777226ull);
#undef CHECK
}
