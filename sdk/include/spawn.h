#ifndef _SPAWN_H
#define _SPAWN_H
#ifdef __cplusplus
extern "C" {
#endif
/* starting programs: apps cannot start other processes (ENOSYS) */
#include <sys/types.h>
#include <signal.h>
typedef struct { int __flags; } posix_spawnattr_t;
typedef struct { int __n; } posix_spawn_file_actions_t;
#define POSIX_SPAWN_SETSIGMASK 0x08
#define POSIX_SPAWN_SETSIGDEF  0x04
int posix_spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fa,
                const posix_spawnattr_t* attr, char* const argv[], char* const envp[]);
int posix_spawnp(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fa,
                 const posix_spawnattr_t* attr, char* const argv[], char* const envp[]);
int posix_spawnattr_init(posix_spawnattr_t* a);
int posix_spawnattr_destroy(posix_spawnattr_t* a);
int posix_spawn_file_actions_init(posix_spawn_file_actions_t* fa);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* fa);
#ifdef __cplusplus
}
#endif
#endif
