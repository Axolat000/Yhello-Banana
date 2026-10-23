/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual resolver with deterministic UDP/TCP and clock shims. No live network. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"
#include "ipv6.h"
#include "tcp.h"

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static uint32_t now, udp_delay, io_delay, chunk, connects, closes, allocations;
static int mode, over6, fail_connect, fail_send, fail_recv, fail_alloc, bad_id, bad_question, tcp_tc, nxdomain;
static uint32_t eof_at;
static uint16_t frame_len;
static uint32_t rx_len, rx_pos, tx_len, udp_sends;
static uint8_t rx[5000], tx[302];
static udp_handler_t cb;
static udp6_handler_t cb6;
static void* cb_ctx;
static netdev_t dev;
static netif_t nif = {.dev = &dev, .dns = IP4(10, 0, 2, 3)};
static ip6_t server6 = {{0x20, 1, 0xd, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x53}};
struct tcp_conn { int unused; };
static struct tcp_conn conn;
netif_t* net_if(void) { return &nif; }
uint32_t timer_ms(void) { return now; }
uint32_t random_u32(void) { return 1234; }
int net_wait_configured(uint32_t t) { (void)t; return 1; }
int net_interrupted(void) { return 0; }
void net_wait(uint32_t t) { now += t; }
int ipv6_enabled(void) { return 1; }
int ipv6_dns(ip6_t* out) { *out = server6; return over6; }
int ip6_parse(const char* s, ip6_t* out) {
    if (strcmp(s, "::1")) return 0;
    memset(out, 0, sizeof(*out)); out->b[15] = 1; return 1;
}
int str_to_ip4(const char* s, ip4_t* out) {
    unsigned a, b, c, d; char tail;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || (a|b|c|d) > 255) return 0;
    *out = IP4(a,b,c,d); return 1;
}
size_t kstrlcpy(char* dst, const char* src, size_t n) {
    size_t len = strlen(src);
    if (n) { size_t k = len < n - 1 ? len : n - 1; memcpy(dst, src, k); dst[k] = 0; }
    return len;
}
void* kmalloc(size_t n) { if (fail_alloc) return NULL; allocations++; return malloc(n); }
void kfree(void* p) { CHECK(allocations > 0); allocations--; free(p); }
uint16_t udp_ephemeral_port(void) { return 45000; }
uint16_t udp6_ephemeral_port(void) { return 45001; }
int udp_bind(uint16_t p, udp_handler_t f, void* ctx) { CHECK(p == 45000); cb = f; cb_ctx = ctx; return 0; }
int udp6_bind(uint16_t p, udp6_handler_t f, void* ctx) { CHECK(p == 45001); cb6 = f; cb_ctx = ctx; return 0; }
void udp_unbind(uint16_t p) { CHECK(p == 45000); cb = NULL; }
void udp6_unbind(uint16_t p) { CHECK(p == 45001); cb6 = NULL; }

static uint32_t answer(uint8_t* rep, const uint8_t* req, uint32_t len, int truncated) {
    memcpy(rep, req, len);
    wr16(rep + 2, truncated ? 0x8380 : nxdomain ? 0x8183 : 0x8180);
    wr16(rep + 6, truncated ? 0 : 1);
    if (truncated) return len;
    uint32_t o = len;
    rep[o++] = 0xc0; rep[o++] = 12;
    uint16_t type = rd16(req + len - 4);
    wr16(rep + o, type); o += 2;
    wr16(rep + o, 1); o += 2;
    wr32(rep + o, 60); o += 4;
    wr16(rep + o, type == 28 ? 16 : 4); o += 2;
    if (type == 28) { memcpy(rep + o, server6.b, 16); o += 16; }
    else { wr32(rep + o, IP4(1,2,3,4)); o += 4; }
    return o;
}
static int send_udp(const void* data, uint32_t len, int v6) {
    uint8_t rep[512]; udp_sends++; now += udp_delay;
    uint32_t n = answer(rep, data, len, mode != 0);
    if (v6) cb6(&server6, 53, rep, n, cb_ctx);
    else cb(nif.dns, 53, rep, n, cb_ctx);
    return NET_OK;
}
int udp_send(ip4_t dst, uint16_t s, uint16_t d, const void* p, uint32_t n) {
    CHECK(dst == nif.dns && s == 45000 && d == 53); return send_udp(p,n,0);
}
int udp6_send(const ip6_t* dst, uint16_t s, uint16_t d, const void* p, uint32_t n) {
    CHECK(!memcmp(dst, &server6, 16) && s == 45001 && d == 53); return send_udp(p,n,1);
}
static int advance(uint32_t budget) {
    CHECK(budget > 0);
    if (io_delay >= budget) { now += budget; return 0; }
    now += io_delay; return 1;
}
tcp_conn_t* tcp_connect(ip4_t ip, uint16_t port, uint32_t t, int* err) {
    CHECK(ip == nif.dns && port == 53 && !cb && !cb6); connects++;
    if (!advance(t) || fail_connect) { *err = NET_ERR_TIMEOUT; return NULL; }
    return &conn;
}
tcp_conn_t* tcp_connect6(const ip6_t* ip, uint16_t port, uint32_t t, int* err) {
    CHECK(!memcmp(ip, &server6, 16)); return tcp_connect(nif.dns, port, t, err);
}
int tcp_send(tcp_conn_t* c, const void* p, uint32_t n, uint32_t t) {
    CHECK(c == &conn);
    if (!advance(t)) return NET_ERR_TIMEOUT;
    if (fail_send) return NET_ERR_INTR;
    if (n > chunk) n = chunk;
    CHECK(tx_len + n <= sizeof(tx)); memcpy(tx + tx_len, p, n); tx_len += n;
    if (tx_len >= 2 && tx_len == (uint32_t)rd16(tx) + 2) {
        rx_len = answer(rx + 2, tx + 2, tx_len - 2, tcp_tc);
        if (bad_id) rx[2] ^= 1;
        if (bad_question) rx[15] ^= 1;
        if (frame_len) rx_len = frame_len;
        wr16(rx, (uint16_t)rx_len); rx_len += 2;
    }
    return (int)n;
}
int tcp_recv(tcp_conn_t* c, void* p, uint32_t n, uint32_t t) {
    CHECK(c == &conn);
    if (!advance(t)) return NET_ERR_TIMEOUT;
    if (fail_recv) return NET_ERR_INTR;
    if (rx_pos == rx_len || rx_pos >= eof_at) return 0;
    if (n > chunk) n = chunk;
    if (n > rx_len - rx_pos) n = rx_len - rx_pos;
    if (n > eof_at - rx_pos) n = eof_at - rx_pos;
    memcpy(p, rx + rx_pos, n); rx_pos += n; return (int)n;
}
void tcp_close(tcp_conn_t* c) { CHECK(c == &conn); closes++; }

#include "../../net/dns.c"
static void reset(void) {
    memset(g_cache, 0, sizeof(g_cache)); memset(rx, 0, sizeof(rx));
    now = udp_delay = io_delay = connects = closes = rx_len = rx_pos = tx_len = udp_sends = 0;
    mode = 1; over6 = fail_connect = fail_send = fail_recv = fail_alloc = bad_id = bad_question = tcp_tc = nxdomain = 0;
    eof_at = 65535;
    frame_len = 0; chunk = 65535; cb = NULL; cb6 = NULL;
    CHECK(allocations == 0);
}
static int query(uint32_t timeout) {
    ip4_t out = 0;
    int rc = dns_resolve("example.test", &out, timeout);
    if (!rc) CHECK(out == IP4(1,2,3,4));
    CHECK(!cb && !cb6 && allocations == 0);
    CHECK(closes == connects - (fail_connect ? 1u : 0u));
    return rc;
}
int main(void) {
    reset(); mode = 0; CHECK(query(1000) == NET_OK && connects == 0);
    reset(); chunk = 1; CHECK(query(1000) == NET_OK && connects == 1);
    CHECK(query(1000) == NET_OK && connects == 1 && udp_sends == 1); /* cached */
    reset(); over6 = 1; chunk = 1;
    ip6_t out; CHECK(dns_resolve6("example.test", &out, 1000) == NET_OK);
    CHECK(!memcmp(&out, &server6, 16) && closes == 1 && !cb6 && allocations == 0);
    reset(); over6 = 0; CHECK(dns_resolve6("example.test", &out, 1000) == NET_OK);
    CHECK(!memcmp(&out, &server6, 16) && closes == 1); /* AAAA over IPv4 */
    reset(); fail_connect = 1; CHECK(query(1000) == NET_ERR_TIMEOUT);
    reset(); fail_send = 1; CHECK(query(1000) == NET_ERR_INTR);
    reset(); fail_recv = 1; CHECK(query(1000) == NET_ERR_INTR);
    reset(); fail_alloc = 1; CHECK(query(1000) == NET_ERR_NOMEM);
    reset(); frame_len = 11; CHECK(query(1000) == NET_ERR_DNS);
    reset(); frame_len = 4097; CHECK(query(1000) == NET_ERR_DNS);
    reset(); frame_len = 4096; CHECK(query(1000) == NET_OK);
    reset(); bad_id = 1; CHECK(query(1000) == NET_ERR_DNS);
    reset(); bad_question = 1; CHECK(query(1000) == NET_ERR_DNS);
    reset(); tcp_tc = 1; CHECK(query(1000) == NET_ERR_DNS);
    reset(); eof_at = 1; CHECK(query(1000) == NET_ERR_CLOSED);
    reset(); eof_at = 15; CHECK(query(1000) == NET_ERR_CLOSED);
    reset(); nxdomain = 1; CHECK(query(1000) == NET_ERR_DNS);
    nxdomain = 0; tx_len = rx_len = rx_pos = 0;
    CHECK(query(1000) == NET_OK && connects == 2); /* no cache entry on failure */
    reset(); udp_delay = 1000; CHECK(query(1000) == NET_ERR_TIMEOUT && connects == 0);
    reset(); chunk = 1; io_delay = 10; CHECK(query(100) == NET_ERR_TIMEOUT && now == 100);
    reset(); now = 0xfffffff0u; chunk = 1; io_delay = 1;
    CHECK(query(1000) == NET_OK && now < 1000);
    printf("DNS TCP: %d checks passed\n", checks);
    return 0;
}
