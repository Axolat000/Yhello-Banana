/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IOWAIT_H
#define IOWAIT_H

#include "types.h"

/*
 * Waiting for a disk (kernel/iowait.c). The disk drivers used to spin on
 * their registers until a command was done - and the kernel runs on the
 * boot core only, so nothing else ran meanwhile: no drawing, no network.
 * Now a driver waits with io_wait: a short spin (fast disks are done in
 * microseconds), then the task sleeps and the device's interrupt wakes it
 * (io_wake) - or, if the interrupt never comes (a machine whose interrupt
 * routing does not work), it looks again every 2 ms. Drivers without an
 * interrupt let the other tasks run between looks.
 *
 * Where a task may not sleep - interrupts off, another processor core,
 * before the scheduler starts, a crash - it spins as before.
 *
 * Since a task can now be switched out in the middle of a command, every
 * device takes its lock (io_lock) around its commands, and a filesystem
 * driver around its operations.
 */

typedef struct {
    volatile int pid;                   /* the task asleep for the device (-1: none) */
    volatile int busy;                  /* io_lock */
} iowait_t;

#define IOWAIT_INIT { -1, 0 }

/* 1 if the calling code may sleep */
int  io_can_sleep(void);
/* waits until done(ctx) is true; irq: the device interrupts when it is
 * (io_wake). 0, or -1 if timeout_ms passed first */
int  io_wait(iowait_t* w, int (*done)(void* ctx), void* ctx, uint32_t timeout_ms, int irq);
/* the device's interrupt: wake the task waiting for it */
void io_wake(iowait_t* w);
/* one step of a wait a driver polls itself (kernel/ata.c): the other tasks
 * run, then the processor sleeps until the next interrupt - where the task
 * may not sleep, a pause */
void io_wait_step(void);

/* since boot: waits that slept, and wake-ups by a device's interrupt
 * (selftest disk: are interrupts arriving?) */
void io_stats(uint32_t* sleeps, uint32_t* irq_wakes);

/* one command (or filesystem operation) at a time */
void io_lock(iowait_t* w);
void io_unlock(iowait_t* w);

#endif
