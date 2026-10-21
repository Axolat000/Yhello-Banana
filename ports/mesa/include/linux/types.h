/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS: the fixed-width types Linux's DRM headers (drm.h, i915_drm.h) use */
#ifndef BANANA_LINUX_TYPES_H
#define BANANA_LINUX_TYPES_H
typedef signed char        __s8;
typedef unsigned char      __u8;
typedef signed short       __s16;
typedef unsigned short     __u16;
typedef signed int         __s32;
typedef unsigned int       __u32;
typedef signed long long   __s64;
typedef unsigned long long __u64;
typedef __u16 __le16;
typedef __u32 __le32;
typedef __u64 __le64;
typedef __u16 __be16;
typedef __u32 __be32;
typedef __u64 __be64;
typedef unsigned long      __kernel_size_t;
typedef long               __kernel_ssize_t;
typedef unsigned long      __kernel_ulong_t;
#endif
