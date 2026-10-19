/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SYS_SENDFILE_H
#define _SYS_SENDFILE_H
#ifdef __cplusplus
extern "C" {
#endif
/* copy count bytes from file in (at *offset, or its position) to out */
#include <sys/types.h>
ssize_t sendfile(int out, int in, off_t* offset, size_t count);
#define sendfile64 sendfile
#ifdef __cplusplus
}
#endif
#endif
