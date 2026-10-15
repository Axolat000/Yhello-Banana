#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h"

/*
 * Spinlocks, for the few kernel structures code on other processor cores
 * may touch (kernel/app.c: system calls that run where they are made - the
 * heap, apps' memory and page tables). The rest of the kernel still runs on
 * the boot core only.
 *
 * Rules: hold one only briefly, never across anything that may switch tasks
 * (task_yield, task_sleep_ms, waiting for a device) - another task on the
 * same core would spin for ever. spin_lock_irq also keeps interrupts off
 * while it is held, for structures interrupt handlers use too.
 */

typedef struct { volatile int v; } spinlock_t;
#define SPINLOCK_INIT { 0 }

static inline void spin_lock(spinlock_t* l) {
    while (__sync_lock_test_and_set(&l->v, 1))
        while (l->v) __asm__ volatile("pause");
}

static inline void spin_unlock(spinlock_t* l) { __sync_lock_release(&l->v); }

static inline uintptr_t spin_lock_irq(spinlock_t* l) {
    uintptr_t f;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(f) :: "memory");
    spin_lock(l);
    return f;
}

static inline void spin_unlock_irq(spinlock_t* l, uintptr_t flags) {
    spin_unlock(l);
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
}

#endif
