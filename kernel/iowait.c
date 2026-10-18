#include "iowait.h"
#include "task.h"
#include "timer.h"
#include "smp.h"

#define SPIN 4000                       /* pauses before sleeping (tens of microseconds) */

static volatile uint32_t g_sleeps, g_irq_wakes;

void io_stats(uint32_t* sleeps, uint32_t* irq_wakes) {
    if (sleeps) *sleeps = g_sleeps;
    if (irq_wakes) *irq_wakes = g_irq_wakes;
}

static int irqs_on(void) {
    uintptr_t f;
    __asm__ volatile("pushf; pop %0" : "=r"(f));
    return (f & 0x200) != 0;
}

int io_can_sleep(void) {
    return task_ready() && cpu_id() == 0 && irqs_on();
}

int io_wait(iowait_t* w, int (*done)(void*), void* ctx, uint32_t timeout_ms, int irq) {
    for (int i = 0; i < SPIN; i++) {
        if (done(ctx)) return 0;
        __asm__ volatile("pause");
    }
    int sleep = io_can_sleep();
    uint32_t start = timer_ms();
    /* (without interrupts the clock may stand still: a count bounds the spin) */
    uint64_t spins = 0, max_spins = (uint64_t)timeout_ms * 200000u;
    for (;;) {
        if (done(ctx)) return 0;
        if (sleep ? timer_ms() - start > timeout_ms : ++spins > max_spins) return -1;
        if (!sleep) { __asm__ volatile("pause"); continue; }
        if (!irq) { task_yield(); continue; }
        /* asleep until the interrupt - or 2 ms, should it never come (an
         * interrupt between the look and the sleep only costs those 2 ms) */
        w->pid = task_current_pid();
        if (!done(ctx)) { g_sleeps++; task_sleep_ms(2); }
        w->pid = -1;
    }
}

void io_wake(iowait_t* w) {
    int pid = w->pid;
    if (pid >= 0) { g_irq_wakes++; task_wake(pid); }
}

void io_lock(iowait_t* w) {
    if (io_can_sleep()) {
        while (w->busy) task_sleep_ms(1);
    } else {
        /* (only when the holder cannot run - a crash, interrupts off: after a
         * while the command is taken over rather than waited for for ever) */
        for (uint32_t i = 0; w->busy && i < 50000000u; i++) __asm__ volatile("pause");
    }
    w->busy = 1;
}

void io_unlock(iowait_t* w) { w->busy = 0; }
