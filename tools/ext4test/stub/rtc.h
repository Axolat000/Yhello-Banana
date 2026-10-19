/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <time.h>
static inline uint32_t rtc_now_utc(void) { return (uint32_t)time(0); }
