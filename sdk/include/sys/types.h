/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>
typedef long ssize_t;
typedef long off_t;
typedef int pid_t;
typedef unsigned int mode_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int useconds_t;
typedef long suseconds_t;
typedef unsigned long ino_t;
typedef unsigned long dev_t;
typedef unsigned long nlink_t;
typedef long blksize_t;
typedef long blkcnt_t;
typedef int id_t;
#ifdef __x86_64__
typedef long off64_t;
#else
typedef long long off64_t;
#endif
typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
typedef char*          caddr_t;
typedef unsigned long  fsblkcnt_t;
typedef unsigned long  fsfilcnt_t;
#ifdef __cplusplus
}
#endif
#endif
