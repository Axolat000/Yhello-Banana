/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS: Linux's ioctl request numbers (asm-generic/ioctl.h's layout:
 * nr 8 bits, type 8, size 14, direction 2) - what the SDK's ioctl() on
 * /dev/dri/card0 passes to the kernel as it is */
#ifndef BANANA_ASM_IOCTL_H
#define BANANA_ASM_IOCTL_H
#define _IOC_NONE  0U
#define _IOC_WRITE 1U
#define _IOC_READ  2U
#define _IOC(dir, type, nr, size) (((dir) << 30) | ((size) << 16) | ((type) << 8) | (nr))
#define _IO(type, nr)             _IOC(_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, t)         _IOC(_IOC_READ, (type), (nr), sizeof(t))
#define _IOW(type, nr, t)         _IOC(_IOC_WRITE, (type), (nr), sizeof(t))
#define _IOWR(type, nr, t)        _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(t))
#define _IOC_NR(nr)   ((nr) & 0xFF)
#define _IOC_TYPE(nr) (((nr) >> 8) & 0xFF)
#define _IOC_SIZE(nr) (((nr) >> 16) & 0x3FFF)
#define _IOC_DIR(nr)  (((nr) >> 30) & 3)
#endif
