/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HOSTNAME_H
#define HOSTNAME_H
#define HOSTNAME_MAX 63
#define HOSTNAME_DEFAULT "banana-os"
#define HOSTNAME_PATH "/etc/hostname"
/* One DNS label, normalized to lowercase. -1 invalid, -2 write failed. */
int hostname_valid(const char* name);
const char* hostname_get(void);
int hostname_set(const char* name);
void hostname_boot(void);
#endif
