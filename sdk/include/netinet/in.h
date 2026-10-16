#ifndef _NETINET_IN_H
#define _NETINET_IN_H
#include <stdint.h>
#include <sys/socket.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr { in_addr_t s_addr; };               /* network order */
struct sockaddr_in {
    sa_family_t    sin_family;
    in_port_t      sin_port;                        /* network order */
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};

struct in6_addr { union { uint8_t s6_addr[16]; uint16_t s6_addr16[8]; uint32_t s6_addr32[4]; } __in6_u; };
#define s6_addr   __in6_u.s6_addr
#define s6_addr16 __in6_u.s6_addr16
#define s6_addr32 __in6_u.s6_addr32
struct sockaddr_in6 {
    sa_family_t     sin6_family;
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};
#define IN6ADDR_ANY_INIT      { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } } }
#define IN6ADDR_LOOPBACK_INIT { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }
extern const struct in6_addr in6addr_any, in6addr_loopback;
#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46

#define INADDR_ANY       ((in_addr_t)0x00000000)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_NONE      ((in_addr_t)0xffffffff)
#define INADDR_LOOPBACK  ((in_addr_t)0x7f000001)    /* (host order, as on Linux) */

#define IPPROTO_IP   0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17
#define IPPROTO_IPV6 41
#define IPPROTO_RAW  255

#define IP_TOS             1
#define IP_TTL             2
#define IP_MULTICAST_IF    32
#define IP_MULTICAST_TTL   33
#define IP_MULTICAST_LOOP  34
#define IP_ADD_MEMBERSHIP  35
#define IP_DROP_MEMBERSHIP 36
#define IPV6_V6ONLY        26

static inline uint16_t htons(uint16_t v) { return (uint16_t)(v << 8 | v >> 8); }
static inline uint16_t ntohs(uint16_t v) { return (uint16_t)(v << 8 | v >> 8); }
static inline uint32_t htonl(uint32_t v) { return v << 24 | (v & 0xff00) << 8 | (v >> 8 & 0xff00) | v >> 24; }
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }
#endif
