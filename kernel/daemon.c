/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "daemon.h"
#include "timer.h"
#include "types.h"

static uint32_t next_announce_tick = 0;

void daemon_init(void) {
    next_announce_tick = timer_ticks() + 1000; /* first message after ~10s */
}

void daemon_poll(int prompt_idle) {
    (void)prompt_idle;
    /*
     * Keep daemon hook in place, but stay silent for now.
     * The user asked to stop periodic terminal announcements.
     */
    (void)next_announce_tick;
}
