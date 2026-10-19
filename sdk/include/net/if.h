/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _NET_IF_H
#define _NET_IF_H
#ifdef __cplusplus
extern "C" {
#endif
/* network interfaces: Banana OS lists "lo" (1) and "eth0" (2) */
#include <sys/socket.h>
#define IFNAMSIZ    16
#define IFHWADDRLEN 6
#define SIOCGIFNAME    0x8910
#define SIOCGIFCONF    0x8912
#define SIOCGIFFLAGS   0x8913
#define SIOCGIFADDR    0x8915
#define SIOCGIFBRDADDR 0x8919
#define SIOCGIFNETMASK 0x891b
#define SIOCGIFMTU     0x8921
#define SIOCGIFHWADDR  0x8927
#define SIOCGIFINDEX   0x8933
#define IF_NAMESIZE 16
#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_RUNNING     0x40
#define IFF_MULTICAST   0x1000
struct if_nameindex { unsigned int if_index; char* if_name; };
struct ifmap { unsigned long mem_start, mem_end; unsigned short base_addr; unsigned char irq, dma, port; };
struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifru_addr, ifru_dstaddr, ifru_broadaddr, ifru_netmask, ifru_hwaddr;
        short ifru_flags;
        int   ifru_ivalue, ifru_mtu;
        struct ifmap ifru_map;
        char  ifru_slave[IFNAMSIZ], ifru_newname[IFNAMSIZ];
        char* ifru_data;
    } ifr_ifru;
};
#define ifr_addr      ifr_ifru.ifru_addr
#define ifr_dstaddr   ifr_ifru.ifru_dstaddr
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_netmask   ifr_ifru.ifru_netmask
#define ifr_hwaddr    ifr_ifru.ifru_hwaddr
#define ifr_flags     ifr_ifru.ifru_flags
#define ifr_ifindex   ifr_ifru.ifru_ivalue
#define ifr_metric    ifr_ifru.ifru_ivalue
#define ifr_mtu       ifr_ifru.ifru_mtu
#define ifr_data      ifr_ifru.ifru_data
struct ifconf {
    int ifc_len;
    union { char* ifcu_buf; struct ifreq* ifcu_req; } ifc_ifcu;
};
#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req
unsigned int         if_nametoindex(const char* name);
char*                if_indextoname(unsigned int index, char* name);
struct if_nameindex* if_nameindex(void);
void                 if_freenameindex(struct if_nameindex* p);
#ifdef __cplusplus
}
#endif
#endif
