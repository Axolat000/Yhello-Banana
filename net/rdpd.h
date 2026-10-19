/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RDPD_H
#define RDPD_H

#include "net.h"

/*
 * Remote Desktop server (MS-RDPBCGR). Works with standard RDP clients
 * (Windows' mstsc, FreeRDP, the Microsoft Remote Desktop apps):
 *   transport   TLS 1.2 (net/tls12.c); the X.224 negotiation selects
 *               PROTOCOL_SSL and refuses clients that demand NLA
 *   identity    an RSA-2048 key and self-signed certificate, generated
 *               on first start and kept in /etc/rdp/
 *   login       the user "banana" with the `passwd` password, from the
 *               Client Info PDU or a prompt drawn into the screen
 *   screen      the local desktop, as changed 64x64 tiles (fast-path
 *               bitmap updates, uncompressed 32bpp)
 *   input       keyboard (set-1 scancodes) and the mouse
 * Off by default, like sshd; one client at a time.
 */

int  rdpd_start(uint16_t port, char* err, int errcap);   /* 0 or -1 */
void rdpd_stop(void);
int  rdpd_running(void);
uint16_t rdpd_port(void);
void rdpd_print_status(void);
/* "SHA-256 AB:CD:.." of the server certificate (generating it if needed) */
int  rdpd_fingerprint(char* out, int cap);

#endif
