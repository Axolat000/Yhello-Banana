/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <time.h>
static inline uint32_t timer_ms(void) { return (uint32_t)(clock() / (CLOCKS_PER_SEC / 1000)); }
