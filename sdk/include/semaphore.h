/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <time.h>

/* unnamed semaphores (sem_init); named ones (sem_open) are not there */
typedef struct { volatile int value; volatile int waiters; } sem_t;
#define SEM_FAILED ((sem_t*)0)

int sem_init(sem_t* s, int pshared, unsigned value);
int sem_destroy(sem_t* s);
int sem_wait(sem_t* s);
int sem_trywait(sem_t* s);
int sem_timedwait(sem_t* s, const struct timespec* abstime);
int sem_post(sem_t* s);
int sem_getvalue(sem_t* s, int* value);
#ifdef __cplusplus
}
#endif
#endif
