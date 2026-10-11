#ifndef FIREWALL_H
#define FIREWALL_H

#include "types.h"

/*
 * The firewall (off unless turned on: `firewall on`, Settings > Network).
 * Incoming: new connections and pings are refused but for the ports
 * allowed; outgoing: anything, and what answers it comes back in.
 */

#define FW_TCP 6
#define FW_UDP 17

void fw_boot(void);                         /* the saved settings (at boot) */
int  fw_enabled(void);
void fw_enable(int on);
int  fw_allow(int proto, uint16_t port, int allow);   /* 0, or -1 (table full) */
int  fw_allowed(int proto, uint16_t port);
int  fw_ping_allowed(void);
void fw_set_ping(int allow);
int  fw_ports(int proto, uint16_t* out, int max);
uint32_t fw_blocked_count(void);
/* the last blocked attempts, newest first: "tcp 10.0.2.2:5555 -> 22" */
int  fw_log(char lines[][64], int max);

/* the filters (net/ip.c, net/ipv6.c, net/udp.c): 1 = let it in */
int  fw_in_tcp(const char* from, uint16_t dport, int syn_only);
int  fw_in_udp(const char* from, uint16_t sport, uint16_t dport);
int  fw_in_ping(const char* from);
void fw_out_udp(uint16_t sport);            /* we sent from this port: answers may come */

#endif
