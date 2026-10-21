/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SPAWN_H
#define _SPAWN_H
#ifdef __cplusplus
extern "C" {
#endif
/* starting programs: fork + exec in one call (API version 26, 64-bit) */
#include <sys/types.h>
#include <signal.h>
typedef struct {
    int      __flags;
    pid_t    __pgrp;
    sigset_t __mask, __def;
} posix_spawnattr_t;
struct __spawn_act;
typedef struct {
    int                 __n, __cap;
    struct __spawn_act* __acts;
} posix_spawn_file_actions_t;
#define POSIX_SPAWN_RESETIDS      0x01
#define POSIX_SPAWN_SETPGROUP     0x02
#define POSIX_SPAWN_SETSIGDEF     0x04
#define POSIX_SPAWN_SETSIGMASK    0x08
#define POSIX_SPAWN_SETSCHEDPARAM 0x10
#define POSIX_SPAWN_SETSCHEDULER  0x20
#define POSIX_SPAWN_USEVFORK      0x40
#define POSIX_SPAWN_SETSID        0x80
int posix_spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fa,
                const posix_spawnattr_t* attr, char* const argv[], char* const envp[]);
int posix_spawnp(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fa,
                 const posix_spawnattr_t* attr, char* const argv[], char* const envp[]);
int posix_spawnattr_init(posix_spawnattr_t* a);
int posix_spawnattr_destroy(posix_spawnattr_t* a);
int posix_spawnattr_setflags(posix_spawnattr_t* a, short flags);
int posix_spawnattr_getflags(const posix_spawnattr_t* a, short* flags);
int posix_spawnattr_setpgroup(posix_spawnattr_t* a, pid_t pgrp);
int posix_spawnattr_getpgroup(const posix_spawnattr_t* a, pid_t* pgrp);
int posix_spawnattr_setsigmask(posix_spawnattr_t* a, const sigset_t* mask);
int posix_spawnattr_getsigmask(const posix_spawnattr_t* a, sigset_t* mask);
int posix_spawnattr_setsigdefault(posix_spawnattr_t* a, const sigset_t* set);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t* a, sigset_t* set);
int posix_spawn_file_actions_init(posix_spawn_file_actions_t* fa);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* fa);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* fa, int fd, const char* path, int oflag, mode_t mode);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* fa, int fd);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* fa, int fd, int newfd);
int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t* fa, const char* path);
int posix_spawn_file_actions_addfchdir_np(posix_spawn_file_actions_t* fa, int fd);
#ifdef __cplusplus
}
#endif
#endif
