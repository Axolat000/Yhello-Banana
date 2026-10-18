#ifndef IOWAIT_H
#define IOWAIT_H

#include "task.h"
#include "timer.h"

/*
 * Waiting for a disk command (kernel/ata.c, ahci.c, nvme.c): the other
 * tasks run meanwhile, and when none has anything to do the processor
 * sleeps until the next interrupt - the controller's own when it is done.
 * One step of it; the caller looks at the controller between steps.
 */
static inline void io_wait_step(void) {
    uintptr_t fl;
    __asm__ volatile("pushf; pop %0" : "=r"(fl));
    if (!(fl & 0x200)) { __asm__ volatile("pause"); return; }   /* (interrupts off - a crash dump: just poll) */
    task_yield();
    timer_idle();
}

/* one command at a time on a channel / port / controller: a task waiting
 * for one lets others run, and they must not start another meanwhile */
static inline void io_lock(volatile int* l) {
    while (__sync_lock_test_and_set(l, 1)) task_sleep_ms(1);
}
static inline void io_unlock(volatile int* l) { __sync_lock_release(l); }

#endif
