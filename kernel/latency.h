/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LATENCY_H
#define LATENCY_H

#include "types.h"

/* Boot-core telemetry only. Hooks never allocate, print, lock or yield. */
extern volatile int latency_enabled;
void latency_irq_begin(uintptr_t saved_flags);
void latency_irq_end(uintptr_t saved_flags);
uint64_t latency_stamp(void);
void latency_input(uint64_t stamp);
void latency_service(uint64_t stamp); /* hardware IRQ handler chain duration */
void latency_timer(void);           /* timer delivery intervals, including raw-cli gaps */
void latency_drop(void);
void latency_input_poll(void);
void latency_set(int on);          /* on starts a new session; off prints its last sample */
void latency_report(void);          /* serial log, task context only */
void latency_poll(void);            /* sysmon: at most once per second */

#endif
