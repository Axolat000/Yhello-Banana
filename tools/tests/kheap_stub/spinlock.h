#pragma once
#include <stdint.h>
typedef struct { volatile int v; } spinlock_t;
#define SPINLOCK_INIT { 0 }
static inline void spin_lock(spinlock_t* l) { (void)l; }
static inline void spin_unlock(spinlock_t* l) { (void)l; }
static inline uintptr_t spin_lock_irq(spinlock_t* l) { (void)l; return 0; }
static inline void spin_unlock_irq(spinlock_t* l, uintptr_t f) { (void)l; (void)f; }
