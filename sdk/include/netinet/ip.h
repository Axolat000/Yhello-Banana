#ifndef _NETINET_IP_H
#define _NETINET_IP_H
#include <stdint.h>
#include <netinet/in.h>
/* the IPv4 header (raw sockets: Banana OS has none) */
struct ip {
    uint8_t  ip_hl:4, ip_v:4;
    uint8_t  ip_tos;
    uint16_t ip_len, ip_id, ip_off;
    uint8_t  ip_ttl, ip_p;
    uint16_t ip_sum;
    struct in_addr ip_src, ip_dst;
};
#define IPTOS_LOWDELAY    0x10
#define IPTOS_THROUGHPUT  0x08
#define IPTOS_RELIABILITY 0x04
#define IPVERSION 4
#define MAXTTL    255
#endif
