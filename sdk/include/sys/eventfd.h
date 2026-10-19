/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_EVENTFD_H
#define _SYS_EVENTFD_H
#ifdef __cplusplus
extern "C" {
#endif
/* a counter to wake a thread that polls (made by the C library) */
#include <stdint.h>
typedef uint64_t eventfd_t;
#define EFD_SEMAPHORE 1
#define EFD_CLOEXEC   02000000
#define EFD_NONBLOCK  04000
int eventfd(unsigned int initval, int flags);
int eventfd_read(int fd, eventfd_t* value);
int eventfd_write(int fd, eventfd_t value);
#ifdef __cplusplus
}
#endif
#endif
