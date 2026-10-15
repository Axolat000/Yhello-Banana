#include "firewall.h"
#include "ipv6.h"
#include "net.h"
#include "netdev.h"
#include "kstring.h"
#include "timer.h"
#include "serial.h"
#include "settings.h"
#include "tcp.h"
#include "kheap.h"

/*
 * IPv6, the basics a network needs from a host: addresses (fe80:: from the
 * card's MAC, the global one from the router's prefix - SLAAC), neighbor
 * discovery (RFC 4861: who has an address, the router's advertisement,
 * answering for ours), and ICMPv6 echo. Off by default (`ipv6 on`).
 * TCP, UDP and DNS share this path. Off means no IPv6 transport or wire traffic.
 */

#define ETH_TYPE_IP6 0x86DD
#define NH_ICMP6     58
#define NC_MAX       16

static int      g_on;
static uint8_t  g_mac[6];                  /* the card the addresses are made from */
static ip6_t    g_ll, g_global, g_router, g_dns;
static int      g_have_global, g_have_router, g_have_dns, g_plen;
static uint32_t g_rs_ms, g_rs_left;

typedef struct { ip6_t ip; uint8_t mac[6]; int state; uint32_t ms; } nc_t;   /* state: 0 free, 1 asked, 2 known */
static nc_t g_nc[NC_MAX];

/* Bounded queues: never overwrite another connection's first packet while
 * neighbour discovery is in progress. TCP can retransmit if the queue fills. */
#define IP6_QUEUE 16
static struct { uint8_t* pkt; uint32_t len, since; ip6_t hop; } g_pending[IP6_QUEUE];
static struct { uint8_t* pkt; uint32_t len; } g_loop[IP6_QUEUE];

#define ECHO_Q 16
static struct { uint16_t id; ip6_echo_t e; } g_echo[ECHO_Q];
static int g_echo_n;

static const ip6_t ALL_NODES   = { { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } };
static const ip6_t ALL_ROUTERS = { { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 } };

static int eq(const ip6_t* a, const ip6_t* b) { return !memcmp(a, b, 16); }
int ip6_is_loopback(const ip6_t* a) {
    static const ip6_t loop = { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } };
    return eq(a, &loop);
}
static int is_mcast(const ip6_t* a) { return a->b[0] == 0xff; }
static int is_ll(const ip6_t* a) { return a->b[0] == 0xfe && (a->b[1] & 0xc0) == 0x80; }
static int is_zero(const ip6_t* a) { static const ip6_t z; return eq(a, &z); }

static void eui64(ip6_t* a, const uint8_t* mac) {
    a->b[8] = mac[0] ^ 0x02; a->b[9] = mac[1]; a->b[10] = mac[2]; a->b[11] = 0xff;
    a->b[12] = 0xfe; a->b[13] = mac[3]; a->b[14] = mac[4]; a->b[15] = mac[5];
}

static int is_ours(const ip6_t* a) {
    if (ip6_is_loopback(a)) return 1;
    if (eq(a, &g_ll) || (g_have_global && eq(a, &g_global)) || eq(a, &ALL_NODES)) return 1;
    /* our solicited-node multicast ff02::1:ffXX:XXXX */
    return a->b[0] == 0xff && a->b[1] == 0x02 && a->b[11] == 1 && a->b[12] == 0xff &&
           a->b[13] == g_ll.b[13] && a->b[14] == g_ll.b[14] && a->b[15] == g_ll.b[15];
}

static void solicited_node(ip6_t* out, const ip6_t* target) {
    memset(out, 0, sizeof(*out));
    out->b[0] = 0xff; out->b[1] = 0x02; out->b[11] = 1; out->b[12] = 0xff;
    out->b[13] = target->b[13]; out->b[14] = target->b[14]; out->b[15] = target->b[15];
}

/* RFC 8200 section 8.1: transport/ICMPv6 pseudo-header checksum. */
uint16_t ip6_checksum(const ip6_t* src, const ip6_t* dst, uint8_t proto, const void* msg, uint32_t len) {
    uint8_t ph[40];
    memcpy(ph, src, 16);
    memcpy(ph + 16, dst, 16);
    wr32(ph + 32, len);
    ph[36] = ph[37] = ph[38] = 0;
    ph[39] = proto;
    uint32_t s = net_csum_add(0, ph, 40);
    s = net_csum_add(s, msg, len);
    return net_csum_fold(s);
}

static uint16_t csum6(const ip6_t* src, const ip6_t* dst, const uint8_t* msg, uint32_t len) {
    return ip6_checksum(src, dst, NH_ICMP6, msg, len);
}

static void addresses(void);
static void ipv6_input(const uint8_t* p, uint32_t len, int local);

/* ── the neighbor cache ── */

static nc_t* nc_find(const ip6_t* ip) {
    for (int i = 0; i < NC_MAX; i++) if (g_nc[i].state && eq(&g_nc[i].ip, ip)) return &g_nc[i];
    return NULL;
}

static nc_t* nc_put(const ip6_t* ip, const uint8_t* mac) {
    nc_t* e = nc_find(ip);
    if (!e) {
        int oldest = 0;
        for (int i = 0; i < NC_MAX; i++) {
            if (!g_nc[i].state) { oldest = i; break; }
            if (g_nc[i].ms < g_nc[oldest].ms) oldest = i;
        }
        e = &g_nc[oldest];
        e->ip = *ip;
    }
    if (mac) { memcpy(e->mac, mac, 6); e->state = 2; }
    else if (e->state != 2) e->state = 1;
    e->ms = timer_ms();
    return e;
}

static int send_raw(const uint8_t* mac, const ip6_t* src, const ip6_t* dst, uint8_t hops, const uint8_t* icmp, uint32_t len);

static void send_ns(const ip6_t* target) {
    uint8_t m[32];
    memset(m, 0, sizeof(m));
    m[0] = 135;
    memcpy(m + 8, target, 16);
    m[24] = 1; m[25] = 1;                  /* source link-layer address */
    memcpy(m + 26, g_mac, 6);
    ip6_t dst;
    solicited_node(&dst, target);
    const ip6_t* src = g_have_global && !is_ll(target) ? &g_global : &g_ll;
    wr16(m + 2, csum6(src, &dst, m, 32));
    uint8_t mac[6] = { 0x33, 0x33, dst.b[12], dst.b[13], dst.b[14], dst.b[15] };
    send_raw(mac, src, &dst, 255, m, 32);
}

static void send_rs(void) {
    uint8_t m[16];
    memset(m, 0, sizeof(m));
    m[0] = 133;
    m[8] = 1; m[9] = 1;
    memcpy(m + 10, g_mac, 6);
    wr16(m + 2, csum6(&g_ll, &ALL_ROUTERS, m, 16));
    static const uint8_t mac[6] = { 0x33, 0x33, 0, 0, 0, 2 };
    send_raw(mac, &g_ll, &ALL_ROUTERS, 255, m, 16);
}

/* ── sending ── */

static int send_raw(const uint8_t* mac, const ip6_t* src, const ip6_t* dst, uint8_t hops, const uint8_t* icmp, uint32_t len) {
    uint8_t pkt[1520];
    if (len > sizeof(pkt) - 40) return NET_ERR_NOMEM;
    pkt[0] = 0x60; pkt[1] = pkt[2] = pkt[3] = 0;
    wr16(pkt + 4, (uint16_t)len);
    pkt[6] = NH_ICMP6;
    pkt[7] = hops;
    memcpy(pkt + 8, src, 16);
    memcpy(pkt + 24, dst, 16);
    memcpy(pkt + 40, icmp, len);
    return eth_send(mac, ETH_TYPE_IP6, pkt, 40 + len) == 0 ? NET_OK : NET_ERR_NODEV;
}

static void make_packet(uint8_t* p, const ip6_t* src, const ip6_t* dst,
                        uint8_t proto, const void* data, uint32_t len) {
    p[0] = 0x60; p[1] = p[2] = p[3] = 0;
    wr16(p + 4, (uint16_t)len); p[6] = proto; p[7] = 64;
    memcpy(p + 8, src, 16); memcpy(p + 24, dst, 16);
    memcpy(p + 40, data, len);
}

int ipv6_source(const ip6_t* dst, ip6_t* src) {
    if (!g_on) return NET_ERR_NOTREADY;
    if (!dst || !src || is_zero(dst)) return NET_ERR_NOROUTE;
    if (ip6_is_loopback(dst)) { *src = *dst; return NET_OK; }
    if (!net_if()->dev) return NET_ERR_NODEV;
    addresses();
    if (is_ll(dst) || (is_mcast(dst) && (dst->b[1] & 15) <= 2) || eq(dst, &g_ll)) *src = g_ll;
    else if (g_have_global) *src = g_global;
    else return NET_ERR_NOTREADY;
    if (!is_ll(dst) && !is_mcast(dst) && !eq(dst, &g_ll) &&
        !(g_have_global && !memcmp(dst, &g_global, 8)) && !g_have_router)
        return NET_ERR_NOROUTE;
    return NET_OK;
}

int ipv6_send(const ip6_t* src, const ip6_t* dst, uint8_t proto, const void* data, uint32_t len) {
    if (!g_on) return NET_ERR_NOTREADY;
    if (!src || !dst || (len && !data) || len > IP6_MAX_PAYLOAD) return NET_ERR_PROTO;
    int local = ip6_is_loopback(dst) || eq(dst, &g_ll) || (g_have_global && eq(dst, &g_global));
    if (!local && (ip6_is_loopback(src) || ip6_is_loopback(dst))) return NET_ERR_NOROUTE;
    if (local) {
        for (int i = 0; i < IP6_QUEUE; i++) if (!g_loop[i].pkt) {
            uint8_t* p = kmalloc(40 + len);
            if (!p) return NET_ERR_NOMEM;
            make_packet(p, src, dst, proto, data, len);
            g_loop[i].len = 40 + len; g_loop[i].pkt = p;
            return NET_OK;
        }
        return NET_ERR_NOMEM;
    }
    if (!net_if()->dev) return NET_ERR_NODEV;
    uint8_t pkt[1280];
    make_packet(pkt, src, dst, proto, data, len);
    if (is_mcast(dst)) {
        uint8_t mac[6] = { 0x33, 0x33, dst->b[12], dst->b[13], dst->b[14], dst->b[15] };
        return eth_send(mac, ETH_TYPE_IP6, pkt, 40 + len) == 0 ? NET_OK : NET_ERR_NODEV;
    }
    const ip6_t* hop = dst;
    if (!is_ll(dst) && !(g_have_global && !memcmp(dst, &g_global, 8))) {
        if (!g_have_router) return NET_ERR_NOROUTE;
        hop = &g_router;
    }
    nc_t* e = nc_find(hop);
    if (e && e->state == 2)
        return eth_send(e->mac, ETH_TYPE_IP6, pkt, 40 + len) == 0 ? NET_OK : NET_ERR_NODEV;
    int slot;
    for (slot = 0; slot < IP6_QUEUE && g_pending[slot].pkt; slot++);
    if (slot == IP6_QUEUE) return NET_ERR_NOMEM;
    uint8_t* copy = kmalloc(40 + len);
    if (!copy) return NET_ERR_NOMEM;
    memcpy(copy, pkt, 40 + len);
    g_pending[slot].len = 40 + len; g_pending[slot].since = timer_ms();
    g_pending[slot].hop = *hop; g_pending[slot].pkt = copy;
    if (!e || timer_ms() - e->ms >= 1000) { nc_put(hop, NULL); send_ns(hop); }
    return NET_OK;
}

void ipv6_loopback_drain(void) {
    for (int i = 0; i < IP6_QUEUE; i++) if (g_loop[i].pkt) {
        uint8_t* p = g_loop[i].pkt;
        uint32_t len = g_loop[i].len;
        g_loop[i].pkt = NULL;
        if (g_on) ipv6_input(p, len, 1);
        kfree(p);
    }
}

/* an ICMPv6 message to dst (checksum filled in here) */
static int icmp6_send(const ip6_t* dst, uint8_t* msg, uint32_t len) {
    ip6_t src;
    int rc = ipv6_source(dst, &src);
    if (rc != NET_OK) return rc;
    msg[2] = msg[3] = 0;
    wr16(msg + 2, csum6(&src, dst, msg, len));
    return ipv6_send(&src, dst, NH_ICMP6, msg, len);
}

static void flush_pending(const ip6_t* hop, const uint8_t* mac) {
    for (int i = 0; i < IP6_QUEUE; i++) if (g_pending[i].pkt && eq(&g_pending[i].hop, hop)) {
        eth_send(mac, ETH_TYPE_IP6, g_pending[i].pkt, g_pending[i].len);
        kfree(g_pending[i].pkt); g_pending[i].pkt = NULL;
    }
}

/* ── receiving ── */

static void on_ns(const ip6_t* src, const uint8_t* m, uint32_t len) {
    if (len < 24) return;
    ip6_t target;
    memcpy(&target, m + 8, 16);
    if (!eq(&target, &g_ll) && !(g_have_global && eq(&target, &g_global))) return;
    const uint8_t* slla = NULL;
    for (uint32_t o = 24; o + 8 <= len && m[o + 1]; o += (uint32_t)m[o + 1] * 8)
        if (m[o] == 1) slla = m + o + 2;
    if (slla && !is_zero(src)) nc_put(src, slla);
    /* the answer: solicited (unless it was address duplicate detection), override */
    uint8_t a[32];
    memset(a, 0, sizeof(a));
    a[0] = 136;
    a[4] = (uint8_t)(is_zero(src) ? 0x20 : 0x60);
    memcpy(a + 8, &target, 16);
    a[24] = 2; a[25] = 1;                  /* target link-layer address */
    memcpy(a + 26, g_mac, 6);
    const ip6_t* dst = is_zero(src) ? &ALL_NODES : src;
    wr16(a + 2, csum6(&target, dst, a, 32));
    uint8_t mac[6];
    if (is_zero(src)) { mac[0] = mac[1] = 0x33; mac[2] = mac[3] = mac[4] = 0; mac[5] = 1; }
    else if (slla) memcpy(mac, slla, 6);
    else { nc_t* e = nc_find(src); if (!e || e->state != 2) return; memcpy(mac, e->mac, 6); }
    send_raw(mac, &target, dst, 255, a, 32);
}

static void on_na(const uint8_t* m, uint32_t len) {
    if (len < 24) return;
    ip6_t target;
    memcpy(&target, m + 8, 16);
    for (uint32_t o = 24; o + 8 <= len && m[o + 1]; o += (uint32_t)m[o + 1] * 8)
        if (m[o] == 2) {
            nc_put(&target, m + o + 2);
            flush_pending(&target, m + o + 2);
        }
}

static void on_ra(const ip6_t* src, const uint8_t* m, uint32_t len) {
    if (len < 16 || !is_ll(src)) return;
    uint16_t life = rd16(m + 6);
    if (life) { g_router = *src; g_have_router = 1; }
    for (uint32_t o = 16; o + 8 <= len && m[o + 1]; o += (uint32_t)m[o + 1] * 8) {
        const uint8_t* opt = m + o;
        uint32_t olen = (uint32_t)opt[1] * 8;
        if (o + olen > len) break;
        if (opt[0] == 1) {                     /* the router's MAC */
            nc_put(src, opt + 2);
            flush_pending(src, opt + 2);
        } else if (opt[0] == 3 && olen >= 32) {   /* a prefix to make an address from */
            uint8_t plen = opt[2], flags = opt[3];
            if ((flags & 0x40) && plen == 64 && rd32(opt + 4)) {
                ip6_t a;
                memcpy(a.b, opt + 16, 8);
                eui64(&a, g_mac);
                if (!g_have_global || !eq(&a, &g_global)) {
                    char t[48];
                    ip6_format(&a, t, sizeof(t));
                    klog("ipv6: address %s/64 (from the router's prefix)\n", t);
                }
                g_global = a;
                g_plen = 64;
                g_have_global = 1;
            }
        } else if (opt[0] == 25 && olen >= 24) {  /* the router's DNS servers */
            memcpy(&g_dns, opt + 8, 16);
            g_have_dns = 1;
        }
    }
    g_rs_left = 0;
}

static void on_echo_reply(const ip6_t* src, const uint8_t* m, uint32_t len, uint8_t hops) {
    if (len < 8 || g_echo_n >= ECHO_Q) return;
    g_echo[g_echo_n].id = rd16(m + 4);
    g_echo[g_echo_n].e.src = *src;
    g_echo[g_echo_n].e.seq = rd16(m + 6);
    g_echo[g_echo_n].e.bytes = len;
    g_echo[g_echo_n].e.hops = hops;
    g_echo[g_echo_n].e.recv_ms = timer_ms();
    g_echo_n++;
}

static void ipv6_input(const uint8_t* p, uint32_t len, int local) {
    if (!g_on || len < 40 || (p[0] >> 4) != 6) return;
    uint32_t plen = rd16(p + 4);
    if (40 + plen > len) return;
    uint8_t nh = p[6], hops = p[7];
    ip6_t src, dst;
    memcpy(&src, p + 8, 16);
    memcpy(&dst, p + 24, 16);
    if (!local && (ip6_is_loopback(&src) || ip6_is_loopback(&dst))) return;
    if (!hops || is_mcast(&src)) return;
    if (!is_ours(&dst)) return;
    if (ip6_is_loopback(&src) && !ip6_is_loopback(&dst)) return;
    const uint8_t* m = p + 40;
    /* RFC 8200: bounded extension-header walking. No fragment reassembly:
     * accept only atomic fragments; unknown mandatory options are dropped. */
    for (int count = 0; nh == 0 || nh == 60 || nh == 43 || nh == 44; count++) {
        if (count >= 8 || plen < 8) return;
        uint32_t h = nh == 44 ? 8 : ((uint32_t)m[1] + 1) * 8;
        if (h > plen) return;
        if (nh == 44 && (rd16(m + 2) & 0xFFF9)) return;
        if (nh == 43 && m[3]) return;
        if (nh == 0 || nh == 60) {
            for (uint32_t o = 2; o < h;) {
                uint8_t type = m[o];
                if (!type) { o++; continue; }
                if (o + 2 > h || o + 2u + m[o + 1] > h || (type & 0xC0)) return;
                o += 2u + m[o + 1];
            }
        }
        nh = m[0]; m += h; plen -= h;
    }
    if ((nh == IP_PROTO_TCP || nh == IP_PROTO_UDP) &&
        (is_zero(&src) || is_mcast(&dst))) return;
    if (fw_enabled() && !ip6_is_loopback(&src) &&
        !eq(&src, &g_ll) && !(g_have_global && eq(&src, &g_global))) {
        char from[48]; ip6_format(&src, from, sizeof(from));
        if (nh == IP_PROTO_TCP && plen >= 20 &&
            !fw_in_tcp(from, rd16(m + 2), (m[13] & 0x12) == 0x02)) return;
        if (nh == IP_PROTO_UDP && plen >= 8 && !fw_in_udp(from, rd16(m), rd16(m + 2))) return;
    }
    if (nh == IP_PROTO_TCP) { tcp_rx6(&src, &dst, m, plen); return; }
    if (nh == IP_PROTO_UDP) { udp6_rx(&src, &dst, m, plen); return; }
    if (nh != NH_ICMP6 || plen < 4) return;
    if (csum6(&src, &dst, m, plen) != 0 || m[1] != 0) return;
    switch (m[0]) {
    case 128: {                                /* echo request: answered */
        if (is_mcast(&dst) && !eq(&dst, &ALL_NODES)) break;
        if (fw_enabled()) {
            char from[48];
            ip6_format(&src, from, sizeof(from));
            if (!fw_in_ping(from)) break;
        }
        static uint8_t r[1500];
        if (plen > sizeof(r)) break;
        memcpy(r, m, plen);
        r[0] = 129;
        icmp6_send(&src, r, plen);
        break;
    }
    case 129: on_echo_reply(&src, m, plen, hops); break;
    case 134: if (hops == 255) on_ra(&src, m, plen); break;
    case 135: if (hops == 255) on_ns(&src, m, plen); break;
    case 136: if (hops == 255) on_na(m, plen); break;
    }
}

/* the card's addresses (again, if another card became the active one) */
static void addresses(void) {
    netdev_t* nd = net_if()->dev;
    if (!nd || !memcmp(nd->mac, g_mac, 6)) return;
    memcpy(g_mac, nd->mac, 6);
    memset(&g_ll, 0, sizeof(g_ll));
    g_ll.b[0] = 0xfe; g_ll.b[1] = 0x80;
    eui64(&g_ll, g_mac);
    g_have_global = g_have_router = g_have_dns = 0;
    memset(g_nc, 0, sizeof(g_nc));
    for (int i = 0; i < IP6_QUEUE; i++) {
        kfree(g_pending[i].pkt); g_pending[i].pkt = NULL;
    }
    if (nd->set_multicast) nd->set_multicast(nd, 1);
    g_rs_left = 3;
    g_rs_ms = timer_ms() - 4000;
    char t[48];
    ip6_format(&g_ll, t, sizeof(t));
    klog("ipv6: on %s, address %s\n", nd->ifname, t);
}

void ipv6_timer(void) {
    if (!g_on) return;
    addresses();
    for (int i = 0; i < IP6_QUEUE; i++) if (g_pending[i].pkt) {
        uint32_t age = timer_ms() - g_pending[i].since;
        if (age >= 4000) { kfree(g_pending[i].pkt); g_pending[i].pkt = NULL; continue; }
        nc_t* e = nc_find(&g_pending[i].hop);
        if (!e || timer_ms() - e->ms >= 1000) {
            nc_put(&g_pending[i].hop, NULL); send_ns(&g_pending[i].hop);
        }
    }
    if (g_rs_left && timer_ms() - g_rs_ms >= 4000) {   /* asking for the router */
        g_rs_ms = timer_ms();
        g_rs_left--;
        send_rs();
    }
}

int ipv6_enabled(void) { return g_on; }

void ipv6_set_runtime(int on) {
    on = on ? 1 : 0;
    if (on == g_on) return;
    g_on = on;
    memset(g_mac, 0, sizeof(g_mac));
    g_have_global = g_have_router = g_have_dns = 0;
    memset(&g_ll, 0, sizeof(g_ll));
    memset(g_nc, 0, sizeof(g_nc)); g_rs_left = 0; g_echo_n = 0;
    for (int i = 0; i < IP6_QUEUE; i++) {
        kfree(g_pending[i].pkt); g_pending[i].pkt = NULL;
        kfree(g_loop[i].pkt); g_loop[i].pkt = NULL;
    }
    if (!on) { tcp6_disable(); klog("ipv6: off\n"); }
    else addresses();
}

void ipv6_enable(int on) {
    settings_set("ipv6", on ? "on" : "off");
    ipv6_set_runtime(on);
}

void ipv6_boot(void) {
    char v[8];
    if (settings_get("ipv6", v, sizeof(v)) > 0 && !strcmp(v, "on")) g_on = 1;
}

int ipv6_link_local(ip6_t* out) { if (!g_on || is_zero(&g_ll)) return 0; *out = g_ll; return 1; }
int ipv6_global(ip6_t* out, int* plen) { if (!g_on || !g_have_global) return 0; *out = g_global; if (plen) *plen = g_plen; return 1; }
int ipv6_router(ip6_t* out) { if (!g_on || !g_have_router) return 0; *out = g_router; return 1; }
int ipv6_dns(ip6_t* out) { if (!g_on || !g_have_dns) return 0; *out = g_dns; return 1; }

int ipv6_echo_send(const ip6_t* dst, uint16_t id, uint16_t seq, uint32_t data_len) {
    static uint8_t m[IP6_MAX_PAYLOAD];
    if (data_len > sizeof(m) - 8) data_len = sizeof(m) - 8;
    m[0] = 128; m[1] = 0;
    wr16(m + 4, id);
    wr16(m + 6, seq);
    for (uint32_t i = 0; i < data_len; i++) m[8 + i] = (uint8_t)(0x10 + i);
    return icmp6_send(dst, m, 8 + data_len);
}

int ipv6_echo_poll(uint16_t id, ip6_echo_t* out) {
    for (int i = 0; i < g_echo_n; i++)
        if (g_echo[i].id == id) {
            *out = g_echo[i].e;
            g_echo[i] = g_echo[--g_echo_n];
            return 1;
        }
    return 0;
}

/* ── text ── */

static int hexd(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c |= 32;
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

int ip6_parse(const char* s, ip6_t* out) {
    uint16_t w[8];
    int n = 0, gap = -1;
    if (s[0] == ':' && s[1] == ':') { gap = 0; s += 2; }
    while (*s && n < 8) {
        int v = 0, d = 0;
        while (d < 4 && hexd(*s) >= 0) { v = v * 16 + hexd(*s); s++; d++; }
        if (!d) return 0;
        w[n++] = (uint16_t)v;
        if (*s == ':') {
            s++;
            if (*s == ':') { if (gap >= 0) return 0; gap = n; s++; }
            else if (!*s) return 0;
        } else if (*s) return 0;
    }
    if (*s) return 0;
    if (gap < 0 && n != 8) return 0;
    if (gap >= 0 && n > 7) return 0;
    uint16_t full[8] = { 0 };
    if (gap < 0) memcpy(full, w, sizeof(full));
    else {
        for (int i = 0; i < gap; i++) full[i] = w[i];
        for (int i = gap; i < n; i++) full[8 - (n - i)] = w[i];
    }
    for (int i = 0; i < 8; i++) { out->b[2 * i] = (uint8_t)(full[i] >> 8); out->b[2 * i + 1] = (uint8_t)full[i]; }
    return 1;
}

void ip6_format(const ip6_t* a, char* out, int cap) {
    uint16_t w[8];
    for (int i = 0; i < 8; i++) w[i] = (uint16_t)(a->b[2 * i] << 8 | a->b[2 * i + 1]);
    int best = -1, blen = 0;                  /* the longest run of zero words (2 or more) */
    for (int i = 0; i < 8; ) {
        if (w[i]) { i++; continue; }
        int j = i;
        while (j < 8 && !w[j]) j++;
        if (j - i > blen && j - i > 1) { best = i; blen = j - i; }
        i = j;
    }
    int o = 0;
    for (int i = 0; i < 8 && o < cap - 6; i++) {
        if (i == best) { o += ksnprintf(out + o, (size_t)(cap - o), "::"); i += blen - 1; continue; }
        o += ksnprintf(out + o, (size_t)(cap - o), (i && i != best + blen) ? ":%x" : "%x", w[i]);
    }
    if (!o) ksnprintf(out, (size_t)cap, "::");
}

void ipv6_rx(const uint8_t* p, uint32_t len) { ipv6_input(p, len, 0); }
