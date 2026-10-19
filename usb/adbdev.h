/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ADBDEV_H
#define ADBDEV_H

#include "types.h"

/*
 * Android phones and tablets over USB: their ADB interface (USB debugging
 * on) and their fastboot interface (the bootloader) - class ff, subclass
 * 42, protocol 01 / 03. Each is a pair of bulk endpoints; this gives the
 * `adb` and `fastboot` commands (shell/adbcmd.c) a byte stream on them.
 */

#define ADBDEV_MAX      4
#define ADBDEV_ADB      1
#define ADBDEV_FASTBOOT 3

typedef struct {
    int  kind;                       /* ADBDEV_ADB / ADBDEV_FASTBOOT */
    char serial[64];
    char product[48];
    char maker[40];
    uint16_t vid, pid;
} adbdev_info_t;

int adbdev_list(adbdev_info_t* out, int max);               /* the ones plugged in */
/* a handle on the first device of `kind` (with that serial, if given), or -1 */
int adbdev_open(int kind, const char* serial);
int adbdev_present(int h);
/* writes everything: len, or -1 */
int adbdev_write(int h, const void* buf, uint32_t len);
/* up to len bytes - what one USB transfer brought (fastboot: one reply),
 * kept for the next call if more came; 0 if nothing came within
 * timeout_ms, -1 if the device went away or failed */
int adbdev_read(int h, void* buf, uint32_t len, uint32_t timeout_ms);
void adbdev_flush_input(int h);                              /* drops what was read ahead */

#endif
