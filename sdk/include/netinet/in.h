/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _NETINET_IN_H
#define _NETINET_IN_H
#ifdef __cplusplus
extern "C" {
#endif
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
#define IP_HDRINCL         3
#define IP_OPTIONS         4
#define IP_PKTINFO         8
#define IP_MTU_DISCOVER    10
#define IP_UNBLOCK_SOURCE  37
#define IP_BLOCK_SOURCE    38
#define IP_ADD_SOURCE_MEMBERSHIP  39
#define IP_DROP_SOURCE_MEMBERSHIP 40
#define IP_MULTICAST_ALL   49
#define MCAST_JOIN_GROUP          42
#define MCAST_BLOCK_SOURCE        43
#define MCAST_UNBLOCK_SOURCE      44
#define MCAST_LEAVE_GROUP         45
#define MCAST_JOIN_SOURCE_GROUP   46
#define MCAST_LEAVE_SOURCE_GROUP  47
#define IPPROTO_ICMPV6     58
#define IPV6_ADDRFORM        1
#define IPV6_CHECKSUM        7
#define IPV6_FLOWINFO        11
#define IPV6_UNICAST_HOPS    16
#define IPV6_MULTICAST_IF    17
#define IPV6_MULTICAST_HOPS  18
#define IPV6_MULTICAST_LOOP  19
#define IPV6_ADD_MEMBERSHIP  20
#define IPV6_DROP_MEMBERSHIP 21
#define IPV6_JOIN_GROUP      IPV6_ADD_MEMBERSHIP
#define IPV6_LEAVE_GROUP     IPV6_DROP_MEMBERSHIP
#define IPV6_V6ONLY          26
#define IPV6_FLOWINFO_SEND   33
#define IPV6_TCLASS          67

/* multicast group requests (setsockopt; Banana OS has no multicast: they fail) */
struct ip_mreq  { struct in_addr imr_multiaddr, imr_interface; };
struct ip_mreqn { struct in_addr imr_multiaddr, imr_address; int imr_ifindex; };
struct ip_mreq_source { struct in_addr imr_multiaddr, imr_interface, imr_sourceaddr; };
struct ipv6_mreq { struct in6_addr ipv6mr_multiaddr; unsigned int ipv6mr_interface; };
struct group_req { uint32_t gr_interface; struct sockaddr_storage gr_group; };
struct group_source_req { uint32_t gsr_interface; struct sockaddr_storage gsr_group, gsr_source; };

#define IN6_IS_ADDR_UNSPECIFIED(a) (((const uint32_t*)(a))[0] == 0 && ((const uint32_t*)(a))[1] == 0 && \
                                    ((const uint32_t*)(a))[2] == 0 && ((const uint32_t*)(a))[3] == 0)
#define IN6_IS_ADDR_LOOPBACK(a)    (((const uint32_t*)(a))[0] == 0 && ((const uint32_t*)(a))[1] == 0 && \
                                    ((const uint32_t*)(a))[2] == 0 && ((const uint32_t*)(a))[3] == htonl(1))
#define IN6_IS_ADDR_MULTICAST(a)   (((const uint8_t*)(a))[0] == 0xff)
#define IN6_IS_ADDR_LINKLOCAL(a)   (((const uint8_t*)(a))[0] == 0xfe && (((const uint8_t*)(a))[1] & 0xc0) == 0x80)
#define IN6_IS_ADDR_SITELOCAL(a)   (((const uint8_t*)(a))[0] == 0xfe && (((const uint8_t*)(a))[1] & 0xc0) == 0xc0)
#define IN6_IS_ADDR_V4MAPPED(a)    (((const uint32_t*)(a))[0] == 0 && ((const uint32_t*)(a))[1] == 0 && \
                                    ((const uint32_t*)(a))[2] == htonl(0xffff))
#define IN6_IS_ADDR_V4COMPAT(a)    (((const uint32_t*)(a))[0] == 0 && ((const uint32_t*)(a))[1] == 0 && \
                                    ((const uint32_t*)(a))[2] == 0 && ntohl(((const uint32_t*)(a))[3]) > 1)
#define IN6_IS_ADDR_MC_GLOBAL(a)   (IN6_IS_ADDR_MULTICAST(a) && (((const uint8_t*)(a))[1] & 0xf) == 0xe)

static inline uint16_t htons(uint16_t v) { return (uint16_t)(v << 8 | v >> 8); }
static inline uint16_t ntohs(uint16_t v) { return (uint16_t)(v << 8 | v >> 8); }
static inline uint32_t htonl(uint32_t v) { return v << 24 | (v & 0xff00) << 8 | (v >> 8 & 0xff00) | v >> 24; }
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }
#ifdef __cplusplus
}
#endif
#endif
