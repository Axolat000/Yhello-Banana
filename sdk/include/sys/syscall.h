/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H
#ifdef __cplusplus
extern "C" {
#endif
/* Linux system call numbers that ported code asks syscall() for: the SDK
 * answers the few it knows (gettid, futex...), the rest fail with ENOSYS */
#define SYS_read              0
#define SYS_write             1
#define SYS_close             3
#define SYS_mmap              9
#define SYS_getpid            39
#define SYS_gettid            186
#define SYS_futex             202
#define SYS_sched_getaffinity 204
#define SYS_clock_gettime     228
#define SYS_tgkill            234
#define SYS_getrandom         318
#define SYS_membarrier        324
#define __NR_gettid     SYS_gettid
#define __NR_futex      SYS_futex
#define __NR_membarrier SYS_membarrier
#define __NR_getrandom  SYS_getrandom
long syscall(long number, ...);
#ifdef __cplusplus
}
#endif
#endif
