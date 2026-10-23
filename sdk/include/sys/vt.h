/* SPDX-License-Identifier: BSD-2-Clause */
/* <sys/vt.h>: the Linux virtual console's ioctls - Banana OS's consoles
 * (tty1 - tty6) are its own, so these fail (EINVAL): a program that asks
 * learns it is not on a Linux console */
#ifndef _SYS_VT_H
#define _SYS_VT_H
#define VT_OPENQRY    0x5600
#define VT_GETMODE    0x5601
#define VT_SETMODE    0x5602
#define VT_GETSTATE   0x5603
#define VT_ACTIVATE   0x5606
#define VT_WAITACTIVE 0x5607
struct vt_stat { unsigned short v_active, v_signal, v_state; };
struct vt_mode { char mode, waitv; short relsig, acqsig, frsig; };
#endif
