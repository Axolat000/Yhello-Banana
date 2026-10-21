/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _PTY_H
#define _PTY_H
#ifdef __cplusplus
extern "C" {
#endif
/* pseudo-terminals in one call (API version 27) */
#include <sys/types.h>
#include <termios.h>
#include <sys/ioctl.h>
int   openpty(int* amaster, int* aslave, char* name, const struct termios* tio, const struct winsize* ws);
pid_t forkpty(int* amaster, char* name, const struct termios* tio, const struct winsize* ws);
int   login_tty(int fd);
#ifdef __cplusplus
}
#endif
#endif
