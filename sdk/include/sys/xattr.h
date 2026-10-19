/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_XATTR_H
#define _SYS_XATTR_H
#ifdef __cplusplus
extern "C" {
#endif
/* extended attributes: Banana OS files have none (ENOTSUP) */
#include <sys/types.h>
#define XATTR_CREATE  1
#define XATTR_REPLACE 2
ssize_t getxattr(const char* path, const char* name, void* value, size_t size);
ssize_t fgetxattr(int fd, const char* name, void* value, size_t size);
int     setxattr(const char* path, const char* name, const void* value, size_t size, int flags);
int     fsetxattr(int fd, const char* name, const void* value, size_t size, int flags);
int     removexattr(const char* path, const char* name);
int     fremovexattr(int fd, const char* name);
ssize_t listxattr(const char* path, char* list, size_t size);
ssize_t flistxattr(int fd, char* list, size_t size);
#ifdef __cplusplus
}
#endif
#endif
