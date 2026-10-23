/* SPDX-License-Identifier: BSD-2-Clause */
/* <netpacket/packet.h>: AF_PACKET addresses (Linux's) - getifaddrs gives a
 * network interface's hardware address in one */
#ifndef _NETPACKET_PACKET_H
#define _NETPACKET_PACKET_H
struct sockaddr_ll {
    unsigned short sll_family, sll_protocol;
    int            sll_ifindex;
    unsigned short sll_hatype;
    unsigned char  sll_pkttype, sll_halen, sll_addr[8];
};
#endif
