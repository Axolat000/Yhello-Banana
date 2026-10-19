#ifndef BOOTTIME_H
#define BOOTTIME_H

#include "types.h"

/*
 * Where the boot's time goes (kernel/boottime.c): each step of the boot
 * marks when it starts - kernel_main's, the shell's, the desktop's first
 * frame. Before the timer runs only the TSC can say when (its rate is
 * measured afterwards, against the clock); after it, the shared clock.
 * `boottime` shows the steps, how long each took and which took longest;
 * the TSC at the kernel's start also gives an idea of the time the
 * firmware and the boot loader took before it.
 */

/* a step starts now: what (a string that lives on) is logged with the time */
void boottime_mark(const char* what);
/* (kernel_main, right after timer_init: the clock can be read from now on) */
void boottime_clock_started(void);

typedef struct {
    const char* what;
    uint64_t    ns;         /* since the kernel started */
} boot_step_t;

/* the steps so far (at most max), in order: how many */
int      boottime_steps(boot_step_t* out, int max);
/* before the kernel: the firmware and the loader, from the TSC (0: unknown) */
uint64_t boottime_before_ns(void);
/* ... split, from the TSC readings Banana Boot passes ("boottsc="): the
 * firmware, the boot menu, loading the kernel, the rest of the loader's
 * work; 0 if the loader passed none */
int      boottime_loader(uint64_t* firmware_ns, uint64_t* menu_ns, uint64_t* load_ns, uint64_t* other_ns);

#endif
