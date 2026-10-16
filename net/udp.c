#include "firewall.h"
#include "net.h"
#include "kstring.h"
#include "random.h"

/* UDP (RFC 768): a small table of bound ports with receive callbacks. */

#define UDP_BINDINGS 16

typedef struct {
    uint16_t      port;
    udp_handler_t handler;
    void*         ctx;
} udp_binding_t;

static udp_binding_t g_bind[UDP_BINDINGS];

int udp_bind(uint16_t port, udp_handler_t h, void* ctx) {
    int slot = -1;
    for (int i = 0; i < UDP_BINDINGS; i++) {
        if (g_bind[i].handler && g_bind[i].port == port) return -1;
        if (!g_bind[i].handler && slot < 0) slot = i;
    }
    if (slot < 0) return -1;
    g_bind[slot].port = port;
    g_bind[slot].handler = h;
    g_bind[slot].ctx = ctx;
    return 0;
}

void udp_unbind(uint16_t port) {
    for (int i = 0; i < UDP_BINDINGS; i++)
        if (g_bind[i].handler && g_bind[i].port == port) g_bind[i].handler = NULL;
}

uint16_t udp_ephemeral_port(void) {
    for (;;) {
        uint16_t p = (uint16_t)(49152u + random_u32() % 16384u);
        int used = 0;
        for (int i = 0; i < UDP_BINDINGS; i++)
            if (g_bind[i].handler && g_bind[i].port == p) used = 1;
        if (!used) return p;
    }
}

int udp_send(ip4_t dst, uint16_t sport, uint16_t dport, const void* data, uint32_t len) {
    fw_out_udp(sport);                     /* (answers to it may come in through the firewall) */
    netif_t* nif = net_if();
    if (len > IP_MAX_PAYLOAD - 8) return NET_ERR_PROTO;
    uint8_t d[IP_MAX_PAYLOAD];
    wr16(d, sport);
    wr16(d + 2, dport);
    wr16(d + 4, (uint16_t)(8 + len));
    wr16(d + 6, 0);
    memcpy(d + 8, data, len);

    /* checksum over the pseudo header + datagram */
    uint8_t ph[12];
    wr32(ph, (dst >> 24) == 127 ? dst : nif->configured ? nif->ip : 0);   /* loopback: from 127.x */
    wr32(ph + 4, dst);
    ph[8] = 0;
    ph[9] = IP_PROTO_UDP;
    wr16(ph + 10, (uint16_t)(8 + len));
    uint16_t cs = net_csum_fold(net_csum_add(net_csum_add(0, ph, 12), d, 8 + len));
    wr16(d + 6, cs ? cs : 0xFFFF);
    return ip_send(dst, IP_PROTO_UDP, d, 8 + len);
}

void udp_rx(ip4_t src, ip4_t dst, const uint8_t* d, uint32_t len) {
    if (len < 8) return;
    uint16_t ulen = rd16(d + 4);
    if (ulen < 8 || ulen > len) return;
    if (rd16(d + 6) != 0) {
        uint8_t ph[12];
        wr32(ph, src);
        wr32(ph + 4, dst);
        ph[8] = 0;
        ph[9] = IP_PROTO_UDP;
        wr16(ph + 10, ulen);
        if (net_csum_fold(net_csum_add(net_csum_add(0, ph, 12), d, ulen)) != 0) return;
    }
    uint16_t sport = rd16(d), dport = rd16(d + 2);
    for (int i = 0; i < UDP_BINDINGS; i++) {
        if (g_bind[i].handler && g_bind[i].port == dport) {
            g_bind[i].handler(src, sport, d + 8, ulen - 8u, g_bind[i].ctx);
            return;
        }
    }
}

/* IPv6 has a separate port namespace; its UDP checksum is mandatory. */
#include "ipv6.h"
static struct { uint16_t port; udp6_handler_t handler; void* ctx; } g_bind6[UDP_BINDINGS];

int udp6_bind(uint16_t port, udp6_handler_t handler, void* ctx) {
    if (!port || !handler) return NET_ERR_PROTO;
    int slot = -1;
    for (int i = 0; i < UDP_BINDINGS; i++) {
        if (g_bind6[i].handler && g_bind6[i].port == port) return NET_ERR_PROTO;
        if (!g_bind6[i].handler && slot < 0) slot = i;
    }
    if (slot < 0) return NET_ERR_NOMEM;
    g_bind6[slot].port = port; g_bind6[slot].handler = handler; g_bind6[slot].ctx = ctx;
    return NET_OK;
}

void udp6_unbind(uint16_t port) {
    for (int i = 0; i < UDP_BINDINGS; i++)
        if (g_bind6[i].handler && g_bind6[i].port == port) g_bind6[i].handler = NULL;
}

uint16_t udp6_ephemeral_port(void) {
    for (int attempt = 0; attempt < 16384; attempt++) {
        uint16_t port = (uint16_t)(49152 + random_u32() % 16384);
        int used = 0;
        for (int i = 0; i < UDP_BINDINGS; i++)
            if (g_bind6[i].handler && g_bind6[i].port == port) used = 1;
        if (!used) return port;
    }
    return 0;
}

int udp6_send(const ip6_t* dst, uint16_t sport, uint16_t dport, const void* data, uint32_t len) {
    if (!sport || !dport || len > IP6_MAX_PAYLOAD - 8 || (len && !data)) return NET_ERR_PROTO;
    ip6_t src;
    int rc = ipv6_source(dst, &src);
    if (rc != NET_OK) return rc;
    uint8_t d[IP6_MAX_PAYLOAD];
    wr16(d, sport); wr16(d + 2, dport); wr16(d + 4, (uint16_t)(8 + len)); wr16(d + 6, 0);
    memcpy(d + 8, data, len);
    uint16_t sum = ip6_checksum(&src, dst, IP_PROTO_UDP, d, 8 + len);
    wr16(d + 6, sum ? sum : 0xFFFF);
    fw_out_udp(sport);
    return ipv6_send(&src, dst, IP_PROTO_UDP, d, 8 + len);
}

void udp6_rx(const ip6_t* src, const ip6_t* dst, const uint8_t* d, uint32_t len) {
    if (!ipv6_enabled() || len < 8) return;
    uint16_t n = rd16(d + 4);
    if (n < 8 || n > len || !rd16(d + 2) || !rd16(d + 6) ||
        ip6_checksum(src, dst, IP_PROTO_UDP, d, n) != 0) return;
    for (int i = 0; i < UDP_BINDINGS; i++)
        if (g_bind6[i].handler && g_bind6[i].port == rd16(d + 2)) {
            g_bind6[i].handler(src, rd16(d), d + 8, n - 8u, g_bind6[i].ctx);
            return;
        }
}
