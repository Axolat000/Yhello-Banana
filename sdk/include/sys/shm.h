/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_SHM_H
#define _SYS_SHM_H
#ifdef __cplusplus
extern "C" {
#endif
/* System V shared memory: not there (shmget fails with ENOSYS) */
#include <sys/ipc.h>
#include <stddef.h>
#define SHM_RDONLY  010000
#define SHM_HUGETLB 04000
#define SHMLBA 4096
#define SHM_REMAP 040000
#define SHM_R 0400
#define SHM_W 0200
struct shmid_ds { struct ipc_perm shm_perm; size_t shm_segsz; long shm_atime, shm_dtime, shm_ctime; pid_t shm_cpid, shm_lpid; unsigned long shm_nattch; };
int   shmget(key_t key, size_t size, int flags);
void* shmat(int id, const void* addr, int flags);
int   shmdt(const void* addr);
int   shmctl(int id, int cmd, struct shmid_ds* buf);
#ifdef __cplusplus
}
#endif
#endif
