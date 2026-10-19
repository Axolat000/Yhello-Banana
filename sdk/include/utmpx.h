/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _UTMPX_H
#define _UTMPX_H
#ifdef __cplusplus
extern "C" {
#endif
/* who is logged in: nobody is listed (getutxent returns NULL) */
#include <sys/types.h>
#include <sys/time.h>
#define EMPTY         0
#define BOOT_TIME     2
#define USER_PROCESS  7
struct utmpx {
    short          ut_type;
    pid_t          ut_pid;
    char           ut_line[32], ut_id[4], ut_user[32], ut_host[256];
    struct timeval ut_tv;
};
void          setutxent(void);
void          endutxent(void);
struct utmpx* getutxent(void);
#ifdef __cplusplus
}
#endif
#endif
