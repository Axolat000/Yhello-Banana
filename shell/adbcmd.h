/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ADBCMD_H
#define ADBCMD_H

/* adb and fastboot: Android phones over USB (usb/adbdev.c) */
void cmd_adb(int argc, char** argv);
void cmd_fastboot(int argc, char** argv);

#endif
