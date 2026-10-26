/* tools/test_tcp.py: net/tcp.c on Linux, both ends of a connection in one
 * process (as the OS's loopback: 127.0.0.1 to itself), over a simulated
 * link - a one-way delay, a bandwidth, a bottleneck queue and random loss,
 * on a virtual clock. Downloads are checked byte for byte and timed in
 * virtual seconds. Built twice: against the new tcp.c (-DNEW_TCP: window
 * scaling) and the one before it from git. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcp.h"

/* ── the simulated world ── */
static uint64_t g_us;                           /* the virtual clock */
static uint32_t g_rand = 2463534242u;
static uint32_t xs(void) { g_rand ^= g_rand << 13; g_rand ^= g_rand >> 17; g_rand ^= g_rand << 5; return g_rand; }

uint32_t timer_ms(void) { return (uint32_t)(g_us / 1000); }
uint32_t random_u32(void) { return xs(); }
void terminal_writeln(const char* s) { (void)s; }

static netif_t g_if = { (void*)1, IP4(10, 0, 2, 15), 1 };
netif_t* net_if(void) { return &g_if; }
int  net_wait_configured(uint32_t t) { (void)t; return 1; }
int  net_interrupted(void) { return 0; }
uint32_t net_csum_add(uint32_t sum, const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    while (len > 1) { sum += (uint32_t)((p[0] << 8) | p[1]); p += 2; len -= 2; }
    if (len) sum += (uint32_t)(p[0] << 8);
    return sum;
}
uint16_t net_csum_fold(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}
void ip4_to_str(ip4_t ip, char out[16]) { snprintf(out, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); }
int  ipv6_enabled(void) { return 0; }
int  ipv6_source(const ip6_t* d, ip6_t* s) { (void)d; (void)s; return NET_ERR_NOROUTE; }
int  ipv6_send(const ip6_t* s, const ip6_t* d, uint8_t p, const void* x, uint32_t n) { (void)s; (void)d; (void)p; (void)x; (void)n; return 0; }
uint16_t ip6_checksum(const ip6_t* s, const ip6_t* d, uint8_t p, const void* x, uint32_t n) { (void)s; (void)d; (void)p; (void)x; (void)n; return 0; }
void ip6_format(const ip6_t* a, char* out, int cap) { (void)a; snprintf(out, (size_t)cap, "::"); }

/* the link: one per direction (by the segment's source port) */
typedef struct {
    uint64_t free_us;                           /* when the wire is free again */
    uint32_t sent, lost, queue_drops;
} dir_t;
static struct { uint32_t delay_us, bytes_per_ms, loss_per_10k, queue_us; } L;
static dir_t g_dir[2];
static uint16_t g_server_port;

typedef struct pkt { struct pkt* next; uint64_t at; uint32_t len; uint8_t d[]; } pkt_t;
static pkt_t* g_q;                              /* sorted by arrival */

int ip_send(ip4_t dst, uint8_t proto, const void* payload, uint32_t len) {
    (void)dst; (void)proto;
    dir_t* d = &g_dir[rd16((const uint8_t*)payload) == g_server_port];
    d->sent++;
    uint64_t start = d->free_us > g_us ? d->free_us : g_us;
    if (start - g_us > L.queue_us) { d->queue_drops++; return 0; }      /* the bottleneck's queue is full */
    d->free_us = start + (uint64_t)(len + 40) * 1000 / (L.bytes_per_ms ? L.bytes_per_ms : 125000);
    if (L.loss_per_10k && (xs() % 10000 < L.loss_per_10k)) { d->lost++; return 0; }
    pkt_t* p = malloc(sizeof(pkt_t) + len);
    p->at = d->free_us + L.delay_us;
    p->len = len;
    memcpy(p->d, payload, len);
    pkt_t** pp = &g_q;
    while (*pp && (*pp)->at <= p->at) pp = &(*pp)->next;
    p->next = *pp;
    *pp = p;
    return 0;
}

static uint64_t g_last_timer;

/* as the OS's: deliver what arrived, the timers every 10 ms, then the
 * delayed ACKs - waiting (the clock jumps) only if nothing is due */
void net_wait(uint32_t max_ms) {
    if (!(g_q && g_q->at <= g_us)) {
        uint64_t next = g_us + (uint64_t)max_ms * 1000, tick = g_last_timer + 10000;
        if (g_q && g_q->at < next) next = g_q->at;
        if (tick < next) next = tick;
        if (next > g_us) g_us = next;
    }
    while (g_q && g_q->at <= g_us) {
        pkt_t* p = g_q;
        g_q = p->next;
        tcp_rx(IP4(127, 0, 0, 1), IP4(127, 0, 0, 1), p->d, p->len);
        free(p);
    }
    if (g_us - g_last_timer >= 10000) { g_last_timer = g_us; tcp_timer(); }
    tcp_flush();
}

/* ── the test ── */
static int g_fail;
static uint8_t byte_at(uint32_t i) { return (uint8_t)(i * 7u + (i >> 11)); }

typedef struct { double mbps; int ok; uint32_t rcv_window; int ws; } result_t;

static result_t download(uint16_t port, uint32_t total, uint32_t delay_ms, uint32_t mbit, uint32_t loss_per_10k) {
    result_t r = { 0, 0, 0, 0 };
    L.delay_us = delay_ms * 1000;
    L.bytes_per_ms = mbit * 125;
    L.loss_per_10k = loss_per_10k;
    L.queue_us = 2 * L.delay_us + 20000;        /* (a bottleneck queue of about a round trip) */
    memset(g_dir, 0, sizeof(g_dir));
    g_server_port = port;
    int err;
    tcp_listener_t* l = tcp_listen(port, &err);
    uint32_t saved_loss = L.loss_per_10k;
    L.loss_per_10k = 0;                         /* (the handshake gets through) */
    tcp_conn_t* c = tcp_connect(IP4(127, 0, 0, 1), port, 5000, &err);
    tcp_conn_t* s = l ? tcp_accept(l, 1000) : NULL;
    L.loss_per_10k = saved_loss;
    if (!c || !s) { printf("FAIL  connect (err %d)\n", err); g_fail++; return r; }
#ifdef NEW_TCP
    tcp_info_t ci;
    tcp_info(c, &ci);
    r.ws = ci.wscale; r.rcv_window = ci.rcv_window;
#endif
    static uint8_t buf[65536];
    uint32_t sent = 0, got = 0;
    uint64_t t0 = g_us, limit = g_us + 900ull * 1000000;
    int bad = 0;
    while (got < total && g_us < limit && !bad) {
        uint32_t space = tcp_send_space(s);
        if (sent < total && space) {
            uint32_t n = total - sent < space ? total - sent : space;
            if (n > sizeof(buf)) n = sizeof(buf);
            for (uint32_t k = 0; k < n; k++) buf[k] = byte_at(sent + k);
            if (tcp_send(s, buf, n, 0) != (int)n) { printf("FAIL  tcp_send\n"); bad = 1; break; }
            sent += n;
        }
        int n = tcp_recv(c, buf, sizeof(buf), 0);
        if (n > 0) {
            for (int k = 0; k < n; k++) if (buf[k] != byte_at(got + (uint32_t)k)) { printf("FAIL  byte %u wrong\n", got + k); bad = 1; break; }
            got += (uint32_t)n;
            continue;
        }
        if (n != NET_ERR_TIMEOUT) { printf("FAIL  tcp_recv: %d after %u bytes\n", n, got); bad = 1; break; }
        net_wait(1);
    }
    double secs = (double)(g_us - t0) / 1e6;
    r.ok = !bad && got == total;
    r.mbps = secs > 0 ? got / secs / 1e6 : 0;
    if (!r.ok && !bad) printf("FAIL  only %u of %u bytes in %.0f s\n", got, total, secs);
    if (!r.ok) g_fail++;
    tcp_abort(c);
    tcp_abort(s);
    tcp_unlisten(l);
    while (g_q) { pkt_t* p = g_q; g_q = p->next; free(p); }
    return r;
}

#ifdef NEW_TCP
static void test_conformance(void);
#endif

int main(void) {
    static const struct { uint32_t delay, mbit, loss, mib; } sc[] = {
        { 1, 100, 0, 16 }, { 25, 100, 0, 16 }, { 50, 100, 0, 16 }, { 100, 100, 0, 16 },
    };
    uint16_t port = 8000;
    for (unsigned i = 0; i < sizeof(sc) / sizeof(sc[0]); i++) {
        result_t r = download(port++, sc[i].mib << 20, sc[i].delay, sc[i].mbit, sc[i].loss);
        printf("RESULT %u %u %u %u %.3f %d %u %d\n", sc[i].delay * 2, sc[i].mbit, sc[i].loss, sc[i].mib,
               r.mbps, r.ok, r.rcv_window, r.ws);
    }
#ifdef NEW_TCP
    tcp_stats_t st;
    tcp_stats(&st);
    printf("STATS fast %u rto %u fallbacks %u\n", st.fast_rexmit, st.rto_rexmit, st.buf_fallback);
    test_conformance();
#endif
    return g_fail ? 1 : 0;
}

#ifdef NEW_TCP
/* ── RFC 7323 Conformance Checks ── */

static int g_conf_fail = 0;
#define CONF_ASSERT(cond, msg) do { \
    if (!(cond)) { printf("CONF_FAIL: %s (line %d)\n", msg, __LINE__); g_conf_fail++; } \
} while (0)

static void test_conformance(void) {
    printf("Running RFC 7323 and buffer management conformance checks...\n");
    L.delay_us = 0;
    L.bytes_per_ms = 100000;
    L.loss_per_10k = 0;
    L.queue_us = 1000000;
    memset(g_dir, 0, sizeof(g_dir));

    /* 1. Unscaled peer: peer sends SYN with MSS only (no WS option) */
    {
        uint16_t port = 9001;
        int err;
        tcp_listener_t* l = tcp_listen(port, &err);
        CONF_ASSERT(l != NULL, "listen on 9001");

        /* Simulate incoming SYN without WS */
        uint8_t syn_seg[24];
        memset(syn_seg, 0, sizeof(syn_seg));
        wr16(syn_seg, 50000); /* sport */
        wr16(syn_seg + 2, port); /* dport */
        wr32(syn_seg + 4, 1000); /* seq */
        wr32(syn_seg + 8, 0); /* ack */
        syn_seg[12] = (24 / 4) << 4; /* data offset */
        syn_seg[13] = 0x02; /* SYN */
        wr16(syn_seg + 14, 32768); /* wnd: 32768 unscaled */
        syn_seg[20] = 2; syn_seg[21] = 4; wr16(syn_seg + 22, 1460); /* MSS option */
        /* compute TCP checksum */
        uint8_t ph[12];
        wr32(ph, IP4(127, 0, 0, 1)); wr32(ph + 4, IP4(127, 0, 0, 1));
        ph[8] = 0; ph[9] = 6; wr16(ph + 10, sizeof(syn_seg));
        wr16(syn_seg + 16, net_csum_fold(net_csum_add(net_csum_add(0, ph, 12), syn_seg, sizeof(syn_seg))));

        tcp_rx(IP4(127, 0, 0, 1), IP4(127, 0, 0, 1), syn_seg, sizeof(syn_seg));

        /* Our SYN-ACK should be queued in g_q */
        CONF_ASSERT(g_q != NULL, "SYN-ACK sent");
        if (g_q) {
            uint8_t* p = g_q->d;
            uint32_t hl = (uint32_t)(p[12] >> 4) * 4;
            /* Peer offered NO window scale, so our SYN-ACK MUST NOT include window scale option */
            int found_ws = 0;
            for (uint32_t i = 20; i < hl;) {
                if (p[i] == 0) break;
                if (p[i] == 1) { i++; continue; }
                if (i + 1 >= hl || p[i + 1] < 2) break;
                if (p[i] == 3) found_ws = 1;
                i += p[i + 1];
            }
            CONF_ASSERT(!found_ws, "RFC 7323: do not negotiate WS if peer did not offer it");

            /* Complete handshake */
            uint32_t our_seq = rd32(p + 4);
            pkt_t* sack_pkt = g_q; g_q = g_q->next; free(sack_pkt);

            uint8_t ack_seg[20];
            memset(ack_seg, 0, sizeof(ack_seg));
            wr16(ack_seg, 50000);
            wr16(ack_seg + 2, port);
            wr32(ack_seg + 4, 1001);
            wr32(ack_seg + 8, our_seq + 1);
            ack_seg[12] = (20 / 4) << 4;
            ack_seg[13] = 0x10; /* ACK */
            wr16(ack_seg + 14, 32768);
            wr32(ph, IP4(127, 0, 0, 1)); wr32(ph + 4, IP4(127, 0, 0, 1));
            ph[8] = 0; ph[9] = 6; wr16(ph + 10, sizeof(ack_seg));
            wr16(ack_seg + 16, net_csum_fold(net_csum_add(net_csum_add(0, ph, 12), ack_seg, sizeof(ack_seg))));
            tcp_rx(IP4(127, 0, 0, 1), IP4(127, 0, 0, 1), ack_seg, sizeof(ack_seg));

            tcp_conn_t* s = tcp_accept(l, 100);
            CONF_ASSERT(s != NULL, "accepted unscaled connection");
            if (s) {
                tcp_info_t info;
                tcp_info(s, &info);
                CONF_ASSERT(info.wscale == 0, "wscale disabled for unscaled peer");
                CONF_ASSERT(info.snd_ws == 0 && info.rcv_ws == 0, "shift counts are 0");
                CONF_ASSERT(info.snd_window == 32768, "peer window is unscaled");
                tcp_abort(s);
            }
        }
        tcp_unlisten(l);
        while (g_q) { pkt_t* tmp = g_q; g_q = g_q->next; free(tmp); }
    }

    /* 2. Scaled peer with excessive shift count (> 14 clamped to 14 per RFC 7323) */
    {
        uint16_t port = 9002;
        int err;
        tcp_listener_t* l = tcp_listen(port, &err);
        CONF_ASSERT(l != NULL, "listen on 9002");

        uint8_t syn_seg[28];
        memset(syn_seg, 0, sizeof(syn_seg));
        wr16(syn_seg, 50001);
        wr16(syn_seg + 2, port);
        wr32(syn_seg + 4, 2000);
        syn_seg[12] = (28 / 4) << 4;
        syn_seg[13] = 0x02; /* SYN */
        wr16(syn_seg + 14, 1024); /* unscaled window in SYN */
        syn_seg[20] = 2; syn_seg[21] = 4; wr16(syn_seg + 22, 1460); /* MSS */
        syn_seg[24] = 1; /* NOP */
        syn_seg[25] = 3; syn_seg[26] = 3; syn_seg[27] = 20; /* WS shift = 20 (> 14!) */
        uint8_t ph[12];
        wr32(ph, IP4(127, 0, 0, 1)); wr32(ph + 4, IP4(127, 0, 0, 1));
        ph[8] = 0; ph[9] = 6; wr16(ph + 10, sizeof(syn_seg));
        wr16(syn_seg + 16, net_csum_fold(net_csum_add(net_csum_add(0, ph, 12), syn_seg, sizeof(syn_seg))));

        tcp_rx(IP4(127, 0, 0, 1), IP4(127, 0, 0, 1), syn_seg, sizeof(syn_seg));
        CONF_ASSERT(g_q != NULL, "SYN-ACK sent for scaled peer");
        if (g_q) {
            uint8_t* p = g_q->d;
            uint32_t our_seq = rd32(p + 4);
            pkt_t* sack_pkt = g_q; g_q = g_q->next; free(sack_pkt);

            uint8_t ack_seg[20];
            memset(ack_seg, 0, sizeof(ack_seg));
            wr16(ack_seg, 50001);
            wr16(ack_seg + 2, port);
            wr32(ack_seg + 4, 2001);
            wr32(ack_seg + 8, our_seq + 1);
            ack_seg[12] = (20 / 4) << 4;
            ack_seg[13] = 0x10;
            wr16(ack_seg + 14, 2); /* window in data phase */
            wr32(ph, IP4(127, 0, 0, 1)); wr32(ph + 4, IP4(127, 0, 0, 1));
            ph[8] = 0; ph[9] = 6; wr16(ph + 10, sizeof(ack_seg));
            wr16(ack_seg + 16, net_csum_fold(net_csum_add(net_csum_add(0, ph, 12), ack_seg, sizeof(ack_seg))));
            tcp_rx(IP4(127, 0, 0, 1), IP4(127, 0, 0, 1), ack_seg, sizeof(ack_seg));

            tcp_conn_t* s = tcp_accept(l, 100);
            CONF_ASSERT(s != NULL, "accepted scaled connection");
            if (s) {
                tcp_info_t info;
                tcp_info(s, &info);
                CONF_ASSERT(info.wscale == 1, "wscale negotiated");
                CONF_ASSERT(info.snd_ws == 14, "excessive shift count clamped to 14");
                CONF_ASSERT(info.rcv_ws == 4, "our rcv_ws is 4");
                CONF_ASSERT(info.snd_window == (2u << 14), "scaled peer window applied");
                tcp_abort(s);
            }
        }
        tcp_unlisten(l);
        while (g_q) { pkt_t* tmp = g_q; g_q = g_q->next; free(tmp); }
    }

    /* 3. SYN window field unscaled requirement in active open */
    {
        int err;
        tcp_conn_t* c = tcp_connect(IP4(127, 0, 0, 1), 80, 0, &err);
        CONF_ASSERT(g_q != NULL, "active SYN sent");
        if (g_q) {
            uint8_t* p = g_q->d;
            uint16_t wnd_field = rd16(p + 14);
            /* Window field in SYN must be unscaled and <= 65535 */
            CONF_ASSERT(wnd_field == 65535u, "SYN window field is unscaled 65535, NOT shifted");
            pkt_t* tmp = g_q; g_q = g_q->next; free(tmp);
        }
        if (c) tcp_abort(c);
        while (g_q) { pkt_t* tmp = g_q; g_q = g_q->next; free(tmp); }
    }

    /* 4. Malformed options in SYN are safely ignored */
    {
        uint16_t port = 9003;
        int err;
        tcp_listener_t* l = tcp_listen(port, &err);
        CONF_ASSERT(l != NULL, "listen on 9003");

        uint8_t syn_seg[28];
        memset(syn_seg, 0, sizeof(syn_seg));
        wr16(syn_seg, 50002);
        wr16(syn_seg + 2, port);
        wr32(syn_seg + 4, 3000);
        syn_seg[12] = (28 / 4) << 4;
        syn_seg[13] = 0x02; /* SYN */
        wr16(syn_seg + 14, 1024);
        syn_seg[20] = 3; syn_seg[21] = 1; /* Malformed WS option length (len < 2) */
        syn_seg[22] = 0; syn_seg[23] = 0;
        uint8_t ph[12];
        wr32(ph, IP4(127, 0, 0, 1)); wr32(ph + 4, IP4(127, 0, 0, 1));
        ph[8] = 0; ph[9] = 6; wr16(ph + 10, sizeof(syn_seg));
        wr16(syn_seg + 16, net_csum_fold(net_csum_add(net_csum_add(0, ph, 12), syn_seg, sizeof(syn_seg))));

        tcp_rx(IP4(127, 0, 0, 1), IP4(127, 0, 0, 1), syn_seg, sizeof(syn_seg));
        CONF_ASSERT(g_q != NULL, "SYN-ACK sent despite malformed option");
        if (g_q) {
            pkt_t* tmp = g_q; g_q = g_q->next; free(tmp);
        }
        tcp_unlisten(l);
        while (g_q) { pkt_t* tmp = g_q; g_q = g_q->next; free(tmp); }
    }

    /* 5. Passive open connection budget test */
    {
        uint16_t port = 9005;
        int err;
        tcp_listener_t* l = tcp_listen(port, &err);
        CONF_ASSERT(l != NULL, "listen on 9005");

        tcp_conn_t* conns[70];
        int count = 0;
        for (int i = 0; i < 70; i++) {
            uint8_t syn[24];
            memset(syn, 0, sizeof(syn));
            wr16(syn, 40000 + i);
            wr16(syn + 2, port);
            wr32(syn + 4, 10000 + i * 100);
            syn[12] = (24 / 4) << 4;
            syn[13] = 0x02; /* SYN */
            wr16(syn + 14, 16384);
            syn[20] = 2; syn[21] = 4; wr16(syn + 22, 1460);
            uint8_t ph[12];
            wr32(ph, IP4(127, 0, 0, 1)); wr32(ph + 4, IP4(127, 0, 0, 1));
            ph[8] = 0; ph[9] = 6; wr16(ph + 10, sizeof(syn));
            wr16(syn + 16, net_csum_fold(net_csum_add(net_csum_add(0, ph, 12), syn, sizeof(syn))));
            tcp_rx(IP4(127, 0, 0, 1), IP4(127, 0, 0, 1), syn, sizeof(syn));
            if (g_q) {
                uint8_t* p = g_q->d;
                uint32_t our_seq = rd32(p + 4);
                pkt_t* sack_pkt = g_q; g_q = g_q->next; free(sack_pkt);

                uint8_t ack[20];
                memset(ack, 0, sizeof(ack));
                wr16(ack, 40000 + i);
                wr16(ack + 2, port);
                wr32(ack + 4, 10001 + i * 100);
                wr32(ack + 8, our_seq + 1);
                ack[12] = (20 / 4) << 4;
                ack[13] = 0x10;
                wr16(ack + 14, 16384);
                wr32(ph, IP4(127, 0, 0, 1)); wr32(ph + 4, IP4(127, 0, 0, 1));
                ph[8] = 0; ph[9] = 6; wr16(ph + 10, sizeof(ack));
                wr16(ack + 16, net_csum_fold(net_csum_add(net_csum_add(0, ph, 12), ack, sizeof(ack))));
                tcp_rx(IP4(127, 0, 0, 1), IP4(127, 0, 0, 1), ack, sizeof(ack));

                tcp_conn_t* s = tcp_accept(l, 10);
                if (s) conns[count++] = s;
                else break;
            } else {
                break;
            }
        }
        CONF_ASSERT(count <= 64, "connection count bounded");
        CONF_ASSERT(count > 0, "connections accepted");
        printf("CONFORMANCE: established %d connections\n", count);
        for (int i = 0; i < count; i++) tcp_abort(conns[i]);
        tcp_unlisten(l);
        while (g_q) { pkt_t* tmp = g_q; g_q = g_q->next; free(tmp); }
    }
    if (g_conf_fail) {
        printf("CONFORMANCE: FAILED with %d failures\n", g_conf_fail);
        g_fail += g_conf_fail;
    } else {
        printf("CONFORMANCE: all RFC 7323 checks passed!\n");
    }
}
#endif
