/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SCHED_H
#define _SCHED_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <sys/types.h>

struct sched_param { int sched_priority; };
#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2
#define SCHED_BATCH 3
#define SCHED_IDLE  5

int sched_yield(void);
int sched_get_priority_min(int policy);
int sched_get_priority_max(int policy);
int sched_getcpu(void);

/* CPU sets: Banana OS spreads threads over the cores itself; the "affinity"
 * is every core it uses */
#define CPU_SETSIZE 1024
typedef struct { unsigned long bits[CPU_SETSIZE / (8 * sizeof(unsigned long))]; } cpu_set_t;
#define __CPU_W(c) ((c) / (8 * sizeof(unsigned long)))
#define __CPU_B(c) (1ul << ((c) % (8 * sizeof(unsigned long))))
#define CPU_ZERO(s)     do { for (size_t __i = 0; __i < sizeof((s)->bits) / sizeof((s)->bits[0]); __i++) (s)->bits[__i] = 0; } while (0)
#define CPU_SET(c, s)   ((s)->bits[__CPU_W(c)] |= __CPU_B(c))
#define CPU_CLR(c, s)   ((s)->bits[__CPU_W(c)] &= ~__CPU_B(c))
#define CPU_ISSET(c, s) (((s)->bits[__CPU_W(c)] & __CPU_B(c)) != 0)
#define CPU_COUNT(s)    __sched_cpucount(sizeof(cpu_set_t), (s))
#define CPU_ALLOC_SIZE(n) sizeof(cpu_set_t)
#define CPU_ALLOC(n)    ((cpu_set_t*)calloc(1, sizeof(cpu_set_t)))
#define CPU_FREE(s)     free(s)
#define CPU_ZERO_S(sz, s)     CPU_ZERO(s)
#define CPU_SET_S(c, sz, s)   CPU_SET(c, s)
#define CPU_ISSET_S(c, sz, s) CPU_ISSET(c, s)
#define CPU_COUNT_S(sz, s)    __sched_cpucount((sz), (s))
int __sched_cpucount(size_t size, const cpu_set_t* s);
int sched_getaffinity(pid_t pid, size_t size, cpu_set_t* s);
int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t* s);
#ifdef __cplusplus
}
#endif
#endif
