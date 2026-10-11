#ifndef IPV6_H
#define IPV6_H

#include "types.h"

/*
 * IPv6 - off unless turned on (`ipv6 on`, kept in the settings): the
 * link-local address, the global one from the router's advertisement
 * (SLAAC), neighbor discovery, and ICMPv6 echo (ping6, and answering).
 */

typedef struct { uint8_t b[16]; } ip6_t;

void ipv6_boot(void);                       /* on at boot if the setting says so */
int  ipv6_enabled(void);
void ipv6_enable(int on);                   /* and remember it */
void ipv6_rx(const uint8_t* p, uint32_t len);
void ipv6_timer(void);                      /* every ~10 ms (net_poll) */

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
