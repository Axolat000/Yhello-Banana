/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _LIMITS_H
#define _LIMITS_H
#ifdef __cplusplus
extern "C" {
#endif
#define CHAR_BIT   8
#define SCHAR_MIN  (-128)
#define SCHAR_MAX  127
#define UCHAR_MAX  255
#define CHAR_MIN   SCHAR_MIN
#define CHAR_MAX   SCHAR_MAX
#define SHRT_MIN   (-32768)
#define SHRT_MAX   32767
#define USHRT_MAX  65535
#define INT_MIN    (-2147483647 - 1)
#define INT_MAX    2147483647
#define UINT_MAX   4294967295u
#define LONG_MAX   __LONG_MAX__
#ifndef SSIZE_MAX
#define SSIZE_MAX  __LONG_MAX__
#endif
#define LONG_MIN   (-__LONG_MAX__ - 1L)
#define ULONG_MAX  (__LONG_MAX__ * 2UL + 1UL)
#define LLONG_MAX  9223372036854775807LL
#define LLONG_MIN  (-9223372036854775807LL - 1)
#define ULLONG_MAX 18446744073709551615ULL
#define PATH_MAX   512        /* (the kernel's FS_PATH_LEN) */
#define IOV_MAX    1024
#define NAME_MAX   255
#define MB_LEN_MAX 16         /* (UTF-8: 4; glibc's value) */
#define ARG_MAX    131072
#define LINE_MAX   2048
#define PIPE_BUF   4096
#define OPEN_MAX   1024
#define HOST_NAME_MAX 64
#define LOGIN_NAME_MAX 256
#define TTY_NAME_MAX 32
#define SYMLOOP_MAX 8
#define NGROUPS_MAX 65536
#define CHARCLASS_NAME_MAX 14
#define RE_DUP_MAX 0x7fff
#define BC_BASE_MAX 99
#define BC_DIM_MAX 2048
#define BC_SCALE_MAX 99
#define BC_STRING_MAX 1000
#define COLL_WEIGHTS_MAX 255
#define EXPR_NEST_MAX 32
#define WORD_BIT 32
#define LONG_BIT (__SIZEOF_LONG__ * 8)
/* POSIX's smallest values (what any system has) */
#define _POSIX_ARG_MAX       4096
#define _POSIX_CHILD_MAX     25
#define _POSIX_LINK_MAX      8
#define _POSIX_MAX_CANON     255
#define _POSIX_MAX_INPUT     255
#define _POSIX_NAME_MAX      14
#define _POSIX_NGROUPS_MAX   8
#define _POSIX_OPEN_MAX      20
#define _POSIX_PATH_MAX      256
#define _POSIX_PIPE_BUF      512
#define _POSIX_SSIZE_MAX     32767
#define _POSIX_STREAM_MAX    8
#define _POSIX_TZNAME_MAX    6
#define _POSIX_SYMLINK_MAX   255
#define _POSIX_SYMLOOP_MAX   8
#define _POSIX_HOST_NAME_MAX 255
#define _POSIX_LOGIN_NAME_MAX 9
#define _POSIX_TTY_NAME_MAX  9
#define _POSIX_RE_DUP_MAX    255
#define _POSIX2_LINE_MAX     2048
#define _POSIX2_RE_DUP_MAX   255
#define _POSIX2_CHARCLASS_NAME_MAX 14
#define _XOPEN_IOV_MAX       16
#define _XOPEN_NAME_MAX      255
#define _XOPEN_PATH_MAX      1024
#ifdef __cplusplus
}
#endif
#endif
