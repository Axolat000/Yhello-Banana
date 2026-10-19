/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _NETINET_ICMP6_H
#define _NETINET_ICMP6_H
#include <stdint.h>
#include <netinet/in.h>
/* ICMPv6 (Banana OS has no IPv6: never sent) */
struct icmp6_hdr {
    uint8_t  icmp6_type, icmp6_code;
    uint16_t icmp6_cksum;
    union { uint32_t icmp6_un_data32[1]; uint16_t icmp6_un_data16[2]; uint8_t icmp6_un_data8[4]; } icmp6_dataun;
};
#define icmp6_data32 icmp6_dataun.icmp6_un_data32
#define icmp6_data16 icmp6_dataun.icmp6_un_data16
#define icmp6_id     icmp6_data16[0]
#define icmp6_seq    icmp6_data16[1]
#define ICMP6_ECHO_REQUEST 128
#define ICMP6_ECHO_REPLY   129
#define ICMP6_FILTER       1
struct icmp6_filter { uint32_t icmp6_filt[8]; };
#define ICMP6_FILTER_SETBLOCKALL(f) ((void)__builtin_memset(f, 0xff, sizeof(struct icmp6_filter)))
#define ICMP6_FILTER_SETPASS(t, f)  ((((f)->icmp6_filt[(t) >> 5]) &= ~(1u << ((t) & 31))))
#endif
