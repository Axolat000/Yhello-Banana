/* SPDX-License-Identifier: GPL-3.0-or-later */
/* tools/test_tcp.py: what net/tcp.c needs of net/net.h */
#ifndef NET_H
#define NET_H
#include "types.h"
typedef uint32_t ip4_t;
#define IP4(a, b, c, d) (((ip4_t)(a) << 24) | ((ip4_t)(b) << 16) | ((ip4_t)(c) << 8) | (ip4_t)(d))
#define NET_OK            0
#define NET_ERR_TIMEOUT   (-1)
#define NET_ERR_RESET     (-2)
#define NET_ERR_REFUSED   (-3)
#define NET_ERR_NOROUTE   (-4)
#define NET_ERR_NOMEM     (-5)
#define NET_ERR_CLOSED    (-6)
#define NET_ERR_DNS       (-7)
#define NET_ERR_NOTREADY  (-8)
#define NET_ERR_NODEV     (-9)
#define NET_ERR_PROTO     (-10)
#define NET_ERR_INTR      (-11)
typedef struct { void* dev; ip4_t ip; int configured; } netif_t;
netif_t* net_if(void);
void net_wait(uint32_t max_ms);
int  net_wait_configured(uint32_t timeout_ms);
int  net_interrupted(void);
static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
uint32_t net_csum_add(uint32_t sum, const void* data, uint32_t len);
uint16_t net_csum_fold(uint32_t sum);
void ip4_to_str(ip4_t ip, char out[16]);
#define IP_PROTO_TCP  6
int  ip_send(ip4_t dst, uint8_t proto, const void* payload, uint32_t len);
void tcp_rx(ip4_t src, ip4_t dst, const uint8_t* seg, uint32_t len);
void tcp_timer(void);
void tcp_flush(void);
#endif
