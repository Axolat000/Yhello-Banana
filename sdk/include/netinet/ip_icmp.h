/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _NETINET_IP_ICMP_H
#define _NETINET_IP_ICMP_H
#include <stdint.h>
#include <netinet/in.h>
#include <netinet/ip.h>
/* ICMP (ping needs raw sockets: Banana OS has none, so it is never sent) */
struct icmp {
    uint8_t  icmp_type, icmp_code;
    uint16_t icmp_cksum;
    union {
        struct { uint16_t icd_id, icd_seq; } ih_idseq;
        uint32_t ih_void;
    } icmp_hun;
    union {
        struct ip idi_ip;
        uint8_t   id_data[1];
    } icmp_dun;
};
#define icmp_id   icmp_hun.ih_idseq.icd_id
#define icmp_seq  icmp_hun.ih_idseq.icd_seq
#define icmp_ip   icmp_dun.idi_ip
#define icmp_data icmp_dun.id_data
#define ICMP_ECHOREPLY 0
#define ICMP_UNREACH   3
#define ICMP_ECHO      8
#define ICMP_MINLEN    8
#endif
