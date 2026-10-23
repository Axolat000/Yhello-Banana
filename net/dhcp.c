/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "net.h"
#include "kstring.h"
#include "timer.h"
#include "random.h"
#include "hostname.h"
#include "serial.h"

/*
 * DHCP client (RFC 2131). Runs non-blocking from netd's timer tick:
 * DISCOVER -> OFFER -> REQUEST -> ACK, then renews at T1, rebinds at T2
 * and expires at the lease end.
 * Asks the server to broadcast its replies, since we can't receive
 * unicast IP before we have an address.
 */

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_MAGIC       0x63825363u

#define MSG_DISCOVER 1
#define MSG_OFFER    2
#define MSG_REQUEST  3
#define MSG_ACK      5
#define MSG_NAK      6

typedef enum { D_IDLE, D_SELECTING, D_REQUESTING, D_BOUND, D_RENEWING, D_REBINDING } dhcp_state_t;

static dhcp_state_t g_state = D_IDLE;
static uint32_t g_xid;
static uint32_t g_next_ms;        /* next retransmit / renewal / rebinding deadline */
static uint32_t g_backoff_ms;
/* Accumulate unsigned timer deltas at each netd tick. Lease intervals can
 * exceed both the signed deadline range (24 days) and timer_ms's 49-day wrap. */
static uint32_t g_lease_tick_ms;
static uint64_t g_elapsed_ms, g_t1_ms, g_t2_ms, g_expire_ms;
static int      g_is_infinite;    /* 1 if lease is 0xFFFFFFFF (never expires) */
static ip4_t    g_offer_ip, g_offer_server;
static int      g_bound_once;

const char* dhcp_state_str(void) {
    switch (g_state) {
    case D_IDLE:       return "idle";
    case D_SELECTING:  return "discovering";
    case D_REQUESTING: return "requesting";
    case D_BOUND:      return "bound";
    case D_RENEWING:   return "renewing";
    case D_REBINDING:  return "rebinding";
    }
    return "?";
}

static void send_msg(uint8_t type, ip4_t requested, ip4_t server) {
    netif_t* nif = net_if();
    uint8_t p[384];
    memset(p, 0, sizeof(p));
    p[0] = 1;                    /* BOOTREQUEST */
    p[1] = 1;                    /* Ethernet */
    p[2] = 6;
    wr32(p + 4, g_xid);
    wr16(p + 10, g_state == D_RENEWING ? 0 : 0x8000);        /* broadcast flag */
    if (g_state == D_RENEWING || g_state == D_REBINDING) wr32(p + 12, nif->ip);   /* ciaddr */
    memcpy(p + 28, nif->dev->mac, 6);
    wr32(p + 236, DHCP_MAGIC);

    uint8_t* o = p + 240;
    *o++ = 53; *o++ = 1; *o++ = type;
    *o++ = 61; *o++ = 7; *o++ = 1;                    /* client id = MAC */
    memcpy(o, nif->dev->mac, 6); o += 6;
    if (requested && g_state != D_RENEWING && g_state != D_REBINDING) {
        *o++ = 50; *o++ = 4; wr32(o, requested); o += 4;
    }
    if (server && g_state != D_RENEWING && g_state != D_REBINDING) {
        *o++ = 54; *o++ = 4; wr32(o, server); o += 4;
    }
    const char* host = hostname_get();
    uint32_t host_len = (uint32_t)strlen(host);
    *o++ = 12; *o++ = (uint8_t)host_len; memcpy(o, host, host_len); o += host_len;
    *o++ = 55; *o++ = 6; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 51; *o++ = 58; *o++ = 59;  /* mask, router, dns, lease, t1, t2 */
    *o++ = 255;

    udp_send(g_state == D_RENEWING && nif->dhcp_server ? nif->dhcp_server : IP4_BROADCAST, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, p, (uint32_t)(o - p));
}

static void start_discover(void) {
    g_state = D_SELECTING;
    g_xid = random_u32();
    g_backoff_ms = 1000;
    g_next_ms = timer_ms() + g_backoff_ms;
    g_is_infinite = 0;
    send_msg(MSG_DISCOVER, 0, 0);
}

static void on_packet(ip4_t src, uint16_t sport, const uint8_t* p, uint32_t len, void* ctx) {
    (void)src; (void)sport; (void)ctx;
    netif_t* nif = net_if();
    if (len < 240 || p[0] != 2 || rd32(p + 4) != g_xid) return;
    if (memcmp(p + 28, nif->dev->mac, 6) != 0 || rd32(p + 236) != DHCP_MAGIC) return;

    ip4_t yiaddr = rd32(p + 16);
    uint8_t type = 0;
    ip4_t mask = 0, router = 0, dns = 0, server = 0;
    uint32_t lease = 0, t1_opt = 0, t2_opt = 0;
    for (uint32_t i = 240; i < len;) {
        uint8_t opt = p[i];
        if (opt == 255) break;
        if (opt == 0) { i++; continue; }
        if (i + 1 >= len) break;
        uint8_t olen = p[i + 1];
        const uint8_t* v = p + i + 2;
        if (i + 2 + olen > len) break;
        if (opt == 53 && olen >= 1) type = v[0];
        else if (opt == 1 && olen >= 4) mask = rd32(v);
        else if (opt == 3 && olen >= 4) router = rd32(v);
        else if (opt == 6 && olen >= 4) dns = rd32(v);
        else if (opt == 51 && olen >= 4) lease = rd32(v);
        else if (opt == 54 && olen >= 4) server = rd32(v);
        else if (opt == 58 && olen >= 4) t1_opt = rd32(v);
        else if (opt == 59 && olen >= 4) t2_opt = rd32(v);
        i += 2u + olen;
    }

    if (type == MSG_OFFER && g_state == D_SELECTING) {
        g_offer_ip = yiaddr;
        g_offer_server = server;
        g_state = D_REQUESTING;
        g_backoff_ms = 1000;
        g_next_ms = timer_ms() + g_backoff_ms;
        send_msg(MSG_REQUEST, g_offer_ip, g_offer_server);
    } else if (type == MSG_ACK && (g_state == D_REQUESTING || g_state == D_RENEWING || g_state == D_REBINDING)) {
        nif->ip = yiaddr;
        nif->netmask = mask ? mask : IP4(255, 255, 255, 0);
        nif->gateway = router;
        if (dns) nif->dns = dns;
        nif->dhcp_server = server ? server : g_offer_server;
        nif->lease_s = lease ? lease : 3600;
        nif->dhcp = 1;
        nif->configured = 1;
        uint32_t now = timer_ms();
        nif->bound_ms = now;
        g_state = D_BOUND;
        g_lease_tick_ms = now;
        g_elapsed_ms = 0;

        if (nif->lease_s == 0xFFFFFFFFu) {
            g_is_infinite = 1;
        } else {
            g_is_infinite = 0;
            uint64_t lease_ms = (uint64_t)nif->lease_s * 1000u;
            uint64_t t1_ms = (uint64_t)t1_opt * 1000u;
            uint64_t t2_ms = (uint64_t)t2_opt * 1000u;
            if (!t1_ms || t1_ms >= lease_ms) t1_ms = lease_ms / 2;
            if (!t2_ms || t2_ms <= t1_ms || t2_ms >= lease_ms)
                t2_ms = lease_ms * 7 / 8;
            /* Invalid pairs fall back together so T1 < T2 < expiry. */
            if (t1_ms >= t2_ms) {
                t1_ms = lease_ms / 2;
                t2_ms = lease_ms * 7 / 8;
            }
            g_t1_ms = t1_ms;
            g_t2_ms = t2_ms;
            g_expire_ms = lease_ms;
        }

        if (!g_bound_once) {
            char a[16], g[16], d[16];
            ip4_to_str(nif->ip, a); ip4_to_str(nif->gateway, g); ip4_to_str(nif->dns, d);
            klog("dhcp: bound %s gw %s dns %s lease %us\n", a, g, d, nif->lease_s);
        }
        g_bound_once = 1;
        /* announce ourselves (gratuitous ARP) and warm the gateway entry */
        arp_request(nif->ip);
        if (nif->gateway) arp_request(nif->gateway);
    } else if (type == MSG_NAK && (g_state == D_REQUESTING || g_state == D_RENEWING || g_state == D_REBINDING)) {
        nif->configured = 0;
        arp_flush();
        start_discover();
    }
}

void dhcp_start(void) {
    netif_t* nif = net_if();
    if (!nif->dev) return;
    udp_unbind(DHCP_CLIENT_PORT);
    udp_bind(DHCP_CLIENT_PORT, on_packet, NULL);
    nif->configured = 0;
    nif->dhcp = 1;
    start_discover();
}

void dhcp_stop(void) {
    g_state = D_IDLE;
    udp_unbind(DHCP_CLIENT_PORT);
}

void dhcp_timer(void) {
    if (g_state == D_IDLE) return;
    netif_t* nif = net_if();
    uint32_t now = timer_ms();
    g_elapsed_ms += (uint32_t)(now - g_lease_tick_ms);
    g_lease_tick_ms = now;

    /* Check lease expiry if bound, renewing or rebinding (unless infinite) */
    if (!g_is_infinite && (g_state == D_BOUND || g_state == D_RENEWING || g_state == D_REBINDING)) {
        if (g_elapsed_ms >= g_expire_ms) {
            nif->configured = 0;
            arp_flush();
            start_discover();
            return;
        }
    }

    if (g_state == D_BOUND) {
        if (g_is_infinite) return;
        if (g_elapsed_ms >= g_t1_ms) {
            g_state = g_elapsed_ms >= g_t2_ms ? D_REBINDING : D_RENEWING;
            g_xid = random_u32();
            g_backoff_ms = 1000;
            g_next_ms = now + g_backoff_ms;
            send_msg(MSG_REQUEST, 0, 0);
        }
        return;
    }

    if (g_state == D_RENEWING) {
        if (g_elapsed_ms >= g_t2_ms) {
            g_state = D_REBINDING;
            g_backoff_ms = 1000;
            g_next_ms = now + g_backoff_ms;
            send_msg(MSG_REQUEST, 0, 0);
            return;
        }
    }

    if ((int32_t)(now - g_next_ms) < 0) return;

    switch (g_state) {
    case D_SELECTING:
    case D_REQUESTING:
        /* exponential backoff, 1s .. 16s; a lost REQUEST restarts cleanly */
        if (g_backoff_ms < 16000) g_backoff_ms *= 2;
        g_next_ms = now + g_backoff_ms;
        if (g_state == D_SELECTING) send_msg(MSG_DISCOVER, 0, 0);
        else if (g_backoff_ms > 8000) start_discover();
        else send_msg(MSG_REQUEST, g_offer_ip, g_offer_server);
        break;
    case D_RENEWING:
    case D_REBINDING: {
        /* exponential backoff, capped at 16s */
        if (g_backoff_ms < 16000) g_backoff_ms *= 2;
        g_next_ms = now + g_backoff_ms;
        send_msg(MSG_REQUEST, 0, 0);
        break;
    }
    default:
        break;
    }
}
