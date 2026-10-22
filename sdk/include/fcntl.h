/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _FCNTL_H
#define _FCNTL_H
#ifdef __cplusplus
extern "C" {
#endif
/* Banana OS SDK (ports): POSIX file descriptors over the SDK stdio */
#include <sys/types.h>
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_ACCMODE  3
#define O_CREAT    0100
#define O_EXCL     0200
#define O_TRUNC    01000
#define O_APPEND   02000
#define O_NONBLOCK 04000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW  0400000
#define O_NOATIME   01000000
#define O_SYNC      04010000
#define O_DSYNC     010000
#define O_NOCTTY    0400
#define O_NDELAY    O_NONBLOCK
#define AT_FDCWD    (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_SYMLINK_FOLLOW 0x400
#define AT_EACCESS  0x200
#define AT_EMPTY_PATH 0x1000
#define F_GETLK  5
#define F_SETLK  6
#define F_SETLKW 7
#define F_ADD_SEALS 1033
#define F_GET_SEALS 1034
#define F_SEAL_SEAL   1
#define F_SEAL_SHRINK 2
#define F_SEAL_GROW   4
#define F_SEAL_WRITE  8
#define F_RDLCK  0
#define F_WRLCK  1
#define F_UNLCK  2
struct flock { short l_type, l_whence; off_t l_start, l_len; pid_t l_pid; };
#define flock64   flock
#define F_GETLK64  F_GETLK
#define F_SETLK64  F_SETLK
#define F_SETLKW64 F_SETLKW
#define POSIX_FADV_NORMAL 0
#define POSIX_FADV_RANDOM 1
#define POSIX_FADV_SEQUENTIAL 2
#define POSIX_FADV_WILLNEED 3
#define POSIX_FADV_DONTNEED 4
#define POSIX_FADV_NOREUSE 5
int posix_fallocate(int fd, off_t off, off_t len);
int posix_fadvise(int fd, off_t off, off_t len, int advice);
#define O_BINARY   0
#define O_TEXT     0
#define O_RSYNC    O_SYNC
#define O_PATH     010000000
#define O_TMPFILE  020200000
#define O_CLOEXEC  02000000
int open(const char* path, int flags, ...);
/* a folder opened (O_RDONLY, O_DIRECTORY) is a descriptor for the *at calls (API version 28) */
int openat(int dirfd, const char* path, int flags, ...);
#define openat64 openat
int creat(const char* path, mode_t mode);
#define open64 open
#define O_LARGEFILE 0
int fcntl(int fd, int cmd, ...);
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD_CLOEXEC 1030
#define FD_CLOEXEC 1
#ifdef __cplusplus
}
#endif
#endif
