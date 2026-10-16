#ifndef _IFADDRS_H
#define _IFADDRS_H
#ifdef __cplusplus
extern "C" {
#endif
/* the network interfaces: lo and the card (eth0) */
#include <sys/socket.h>
struct ifaddrs {
    struct ifaddrs*  ifa_next;
    char*            ifa_name;
    unsigned int     ifa_flags;
    struct sockaddr* ifa_addr;
    struct sockaddr* ifa_netmask;
    struct sockaddr* ifa_broadaddr;
    void*            ifa_data;
};
#define ifa_dstaddr ifa_broadaddr
int  getifaddrs(struct ifaddrs** out);
void freeifaddrs(struct ifaddrs* ifa);
#ifdef __cplusplus
}
#endif
#endif
