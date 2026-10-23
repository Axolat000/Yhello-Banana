/* SPDX-License-Identifier: BSD-2-Clause */
/* <net/ethernet.h>: Ethernet's frame header and types (Linux's) */
#ifndef _NET_ETHERNET_H
#define _NET_ETHERNET_H
#include <stdint.h>
#define ETH_ALEN      6
#define ETHER_ADDR_LEN ETH_ALEN
#define ETH_HLEN      14
#define ETH_P_IP      0x0800
#define ETH_P_ARP     0x0806
#define ETH_P_IPV6    0x86DD
#define ETH_P_ALL     0x0003
#define ETHERTYPE_IP   ETH_P_IP
#define ETHERTYPE_ARP  ETH_P_ARP
#define ETHERTYPE_IPV6 ETH_P_IPV6
struct ether_addr { uint8_t ether_addr_octet[ETH_ALEN]; };
struct ether_header { uint8_t ether_dhost[ETH_ALEN], ether_shost[ETH_ALEN]; uint16_t ether_type; };
#endif
