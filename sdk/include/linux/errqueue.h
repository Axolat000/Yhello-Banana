/* SPDX-License-Identifier: BSD-2-Clause */
/* <linux/errqueue.h>: a socket's error queue (MSG_ERRQUEUE) - empty here */
#ifndef _LINUX_ERRQUEUE_H
#define _LINUX_ERRQUEUE_H
#include <stdint.h>
struct sock_extended_err {
    uint32_t ee_errno;
    uint8_t  ee_origin, ee_type, ee_code, ee_pad;
    uint32_t ee_info, ee_data;
};
#define SO_EE_ORIGIN_NONE  0
#define SO_EE_ORIGIN_LOCAL 1
#define SO_EE_ORIGIN_ICMP  2
#define SO_EE_ORIGIN_ICMP6 3
#define SO_EE_OFFENDER(ee) ((struct sockaddr*)((ee) + 1))
#endif
