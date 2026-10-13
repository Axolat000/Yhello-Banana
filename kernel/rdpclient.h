#ifndef RDPCLIENT_H
#define RDPCLIENT_H

#include "types.h"

/*
 * Remote Desktop client (MS-RDPBCGR): connects to another RDP server
 * (Windows, xrdp, FreeRDP, Banana OS's own rdpd) over TLS 1.2, with NLA
 * (CredSSP) when credentials are given. Runs as a kernel task that owns a
 * desktop window (kernel/appwin.h); the remote screen is drawn into it,
 * the keyboard (raw set-1 scancodes) and mouse are forwarded.
 *
 * Shell command: rdp [-u user] [-d domain] host[:port]
 */

typedef struct {
    char     host[128];
    uint16_t port;
    char     user[64];
    char     domain[64];
    char     password[128];
    int      nla;                  /* try NLA (CredSSP) first */
} rdp_params_t;

/* spawns the client task (copies *p); 0 if started, -1 if busy / no task */
int rdpclient_launch(const rdp_params_t* p);

#endif
