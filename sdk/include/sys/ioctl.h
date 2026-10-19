/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H
#ifdef __cplusplus
extern "C" {
#endif
#define TIOCGWINSZ 0x5413
#define FIONREAD   0x541B
#define FIONBIO    0x5421
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
/* FIONREAD and FIONBIO on sockets, TIOCGWINSZ on the terminal */
int ioctl(int fd, unsigned long req, ...);
#ifdef __cplusplus
}
#endif
#endif
