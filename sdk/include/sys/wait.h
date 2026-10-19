/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#ifdef __cplusplus
extern "C" {
#endif
/* (apps start no child processes: wait fails with ECHILD) */
#include <sys/types.h>
#include <signal.h>
#define WNOHANG    1
#define WUNTRACED  2
#define WSTOPPED   2
#define WEXITED    4
#define WCONTINUED 8
#define WNOWAIT    0x01000000
typedef enum { P_ALL, P_PID, P_PGID } idtype_t;
#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WTERMSIG(s)    ((s) & 0x7f)
#define WIFEXITED(s)   (WTERMSIG(s) == 0)
#define WIFSIGNALED(s) (((signed char)(((s) & 0x7f) + 1) >> 1) > 0)
#define WIFSTOPPED(s)  (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)    WEXITSTATUS(s)
pid_t wait(int* status);
pid_t waitpid(pid_t pid, int* status, int options);

int   waitid(idtype_t type, id_t id, siginfo_t* info, int options);
#ifdef __cplusplus
}
#endif
#endif
