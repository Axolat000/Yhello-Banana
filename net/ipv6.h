/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IPV6_H
#define IPV6_H

#include "types.h"

/*
 * IPv6 - off unless turned on (`ipv6 on`, kept in the settings): the
 * link-local address, the global one from the router's advertisement
 * (SLAAC), neighbor discovery, ICMPv6 echo, TCP, UDP and AAAA resolution.
 * `toggle6 [on|off]` and Settings control the same saved switch.
 */

typedef struct { uint8_t b[16]; } ip6_t;

/* Conservative MTU: no source fragmentation or path-MTU discovery yet. */
#define IP6_MAX_PAYLOAD 1240u /* IPv6 minimum MTU (1280) minus its header */

void ipv6_boot(void);                       /* on at boot if the setting says so */
int  ipv6_enabled(void);
void ipv6_enable(int on);                   /* and remember it */
void ipv6_set_runtime(int on);              /* tests: do not change saved settings */
void ipv6_rx(const uint8_t* p, uint32_t len);
void ipv6_timer(void);                      /* every ~10 ms (net_poll) */
void ipv6_loopback_drain(void);
int ipv6_source(const ip6_t* dst, ip6_t* src); /* NET_OK or NET_ERR_* */
int ipv6_send(const ip6_t* src, const ip6_t* dst, uint8_t proto, const void* data, uint32_t len);
uint16_t ip6_checksum(const ip6_t* src, const ip6_t* dst, uint8_t proto, const void* data, uint32_t len);
int ip6_is_loopback(const ip6_t* ip);

typedef void (*udp6_handler_t)(const ip6_t* src, uint16_t sport, const uint8_t* data, uint32_t len, void* ctx);
int udp6_bind(uint16_t port, udp6_handler_t handler, void* ctx);
void udp6_unbind(uint16_t port);
uint16_t udp6_ephemeral_port(void);
int udp6_send(const ip6_t* dst, uint16_t sport, uint16_t dport, const void* data, uint32_t len);
void udp6_rx(const ip6_t* src, const ip6_t* dst, const uint8_t* data, uint32_t len);
int dns_resolve6(const char* name, ip6_t* out, uint32_t timeout_ms);

/* the addresses: 0 if there is none */
int  ipv6_link_local(ip6_t* out);
int  ipv6_global(ip6_t* out, int* prefix_len);
int  ipv6_router(ip6_t* out);
int  ipv6_dns(ip6_t* out);

int  ip6_parse(const char* s, ip6_t* out);  /* 1 if valid */
void ip6_format(const ip6_t* a, char* out, int cap);   /* compressed (RFC 5952) */

/* ping6 */
int  ipv6_echo_send(const ip6_t* dst, uint16_t id, uint16_t seq, uint32_t data_len);   /* NET_OK... */
typedef struct { ip6_t src; uint16_t seq; uint32_t bytes; uint8_t hops; uint32_t recv_ms; } ip6_echo_t;
int  ipv6_echo_poll(uint16_t id, ip6_echo_t* out);

#endif
