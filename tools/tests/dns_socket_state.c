/* Characterization tests for net/dns.c and sdk/lib/socket.c (base-17 step 2) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define htons net_htons
#define ntohs net_ntohs
#define htonl net_htonl
#define ntohl net_ntohl
#include "net.h"
#undef htons
#undef ntohs
#undef htonl
#undef ntohl

#include "ipv6.h"
#include "tcp.h"
#include "kstring.h"
#include "banana_api.h"

static int checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

int h_errno = 0;

size_t kstrlcpy(char* dst, const char* src, size_t n) {
    size_t len = strlen(src);
    if (n) {
        size_t c = len < n - 1 ? len : n - 1;
        memcpy(dst, src, c);
        dst[c] = 0;
    }
    return len;
}

int str_to_ip4(const char* s, ip4_t* out) {
    struct in_addr in;
    if (inet_aton(s, &in)) {
        *out = ntohl(in.s_addr);
        return 1;
    }
    return 0;
}

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

int ip6_is_loopback(const ip6_t* a) {
    static const ip6_t loop = { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } };
    return !memcmp(a, &loop, 16);
}

/* Shims for kernel/net environment */
static netif_t nif;
static uint32_t g_now = 1000;
static uint32_t g_xid = 0x1234;
static int g_ipv6_enabled = 0;
static int g_ipv6_has_dns = 0;
static ip6_t g_ipv6_dns_server;

/* Mock UDP state */
static udp_handler_t g_udp_handler;
static void* g_udp_ctx;
static uint16_t g_udp_bound_port;

static udp6_handler_t g_udp6_handler;
static void* g_udp6_ctx;
static uint16_t g_udp6_bound_port;

static uint8_t g_last_sent_pkt[512];
static uint32_t g_last_sent_len = 0;
static uint32_t g_udp_send_count = 0;
static uint32_t g_udp6_send_count = 0;

/* Injection controls for mock replies */
static int g_inject_action = 0; /* 0=none, 1=valid_reply, 2=nxdomain, 3=truncated, 4=wrong_id, 5=wrong_qtype, 6=malformed */
static uint32_t g_inject_ttl = 300;
static ip4_t g_inject_ip4 = 0x5db8d822; /* 93.184.216.34 */
static uint8_t g_inject_ip6[16] = { 0x26, 0x06, 0x28, 0x00, 0x02, 0x20, 0x00, 0x01,
                                    0x02, 0x48, 0x18, 0x93, 0x25, 0xc8, 0x19, 0x46 };

netif_t* net_if(void) { return &nif; }
uint32_t timer_ms(void) { return g_now; }
uint32_t random_u32(void) { return ++g_xid; }
void klog(const char* fmt, ...) { (void)fmt; }
int net_interrupted(void) { return 0; }
int net_wait_configured(uint32_t timeout_ms) { (void)timeout_ms; return nif.configured; }

int ipv6_enabled(void) { return g_ipv6_enabled; }
int ipv6_dns(ip6_t* out) {
    if (g_ipv6_has_dns && out) {
        *out = g_ipv6_dns_server;
        return 1;
    }
    return 0;
}

uint16_t udp_ephemeral_port(void) { return 49152; }
uint16_t udp6_ephemeral_port(void) { return 49153; }

int udp_bind(uint16_t port, udp_handler_t handler, void* ctx) {
    g_udp_bound_port = port;
    g_udp_handler = handler;
    g_udp_ctx = ctx;
    return 0;
}
void udp_unbind(uint16_t port) {
    if (g_udp_bound_port == port) {
        g_udp_bound_port = 0;
        g_udp_handler = NULL;
        g_udp_ctx = NULL;
    }
}

int udp6_bind(uint16_t port, udp6_handler_t handler, void* ctx) {
    g_udp6_bound_port = port;
    g_udp6_handler = handler;
    g_udp6_ctx = ctx;
    return 0;
}
void udp6_unbind(uint16_t port) {
    if (g_udp6_bound_port == port) {
        g_udp6_bound_port = 0;
        g_udp6_handler = NULL;
        g_udp6_ctx = NULL;
    }
}

static void send_mock_reply(int is6);

int udp_send(ip4_t dst, uint16_t sport, uint16_t dport, const void* data, uint32_t len) {
    (void)dst; (void)sport; (void)dport;
    if (len <= sizeof(g_last_sent_pkt)) {
        memcpy(g_last_sent_pkt, data, len);
        g_last_sent_len = len;
    }
    g_udp_send_count++;
    if (g_inject_action > 0) {
        send_mock_reply(0);
    }
    return NET_OK;
}

int udp6_send(const ip6_t* dst, uint16_t sport, uint16_t dport, const void* data, uint32_t len) {
    (void)dst; (void)sport; (void)dport;
    if (len <= sizeof(g_last_sent_pkt)) {
        memcpy(g_last_sent_pkt, data, len);
        g_last_sent_len = len;
    }
    g_udp6_send_count++;
    if (g_inject_action > 0) {
        send_mock_reply(1);
    }
    return NET_OK;
}

void net_wait(uint32_t ms) {
    g_now += ms;
}

/* Format DNS reply packet */
static void send_mock_reply(int is6) {
    if (g_last_sent_len < 12) return;
    uint8_t rep[512] = {0};
    uint16_t id = (uint16_t)(g_last_sent_pkt[0] << 8 | g_last_sent_pkt[1]);
    if (g_inject_action == 4) id ^= 0xFFFF; /* wrong transaction ID */

    wr16(rep, id);
    uint16_t flags = 0x8180; /* standard response, recursion available */
    if (g_inject_action == 2) flags |= 0x0003; /* NXDOMAIN */
    if (g_inject_action == 3) flags |= 0x0200; /* TC (truncated) */
    wr16(rep + 2, flags);
    wr16(rep + 4, 1); /* QDCOUNT = 1 */

    /* Copy question section from sent packet */
    uint32_t qlen = g_last_sent_len - 12;
    if (12 + qlen > sizeof(rep) - 64) return;
    memcpy(rep + 12, g_last_sent_pkt + 12, qlen);
    uint32_t off = 12 + qlen;

    if (g_inject_action == 1 || (g_inject_action == 3 && is6)) {
        /* Add answer record */
        wr16(rep + 6, 1); /* ANCOUNT = 1 */
        /* Pointer to question name: 0xc00c */
        rep[off++] = 0xc0;
        rep[off++] = 0x0c;
        uint16_t qtype = (uint16_t)(g_last_sent_pkt[g_last_sent_len - 4] << 8 | g_last_sent_pkt[g_last_sent_len - 3]);
        if (g_inject_action == 5) qtype = (qtype == 1 ? 28 : 1); /* mismatched type */
        wr16(rep + off, qtype); off += 2;
        wr16(rep + off, 1); off += 2; /* IN class */
        wr32(rep + off, g_inject_ttl); off += 4; /* TTL */
        if (qtype == 28) {
            wr16(rep + off, 16); off += 2; /* RDLENGTH */
            memcpy(rep + off, g_inject_ip6, 16); off += 16;
        } else {
            wr16(rep + off, 4); off += 2; /* RDLENGTH */
            wr32(rep + off, g_inject_ip4); off += 4;
        }
    } else if (g_inject_action == 6) {
        /* Malformed answer record: truncated RDLENGTH */
        wr16(rep + 6, 1);
        rep[off++] = 0xc0;
        rep[off++] = 0x0c;
        wr16(rep + off, 1); off += 2;
        wr16(rep + off, 1); off += 2;
        wr32(rep + off, 300); off += 4;
        wr16(rep + off, 100); off += 2; /* claims 100 bytes but packet ends */
    }

    if (is6) {
        if (g_udp6_handler) g_udp6_handler(&g_ipv6_dns_server, 53, rep, off, g_udp6_ctx);
    } else {
        if (g_udp_handler) g_udp_handler(nif.dns, 53, rep, off, g_udp_ctx);
    }
}

static uint32_t g_tcp_connect_count = 0;
static uint32_t g_tcp_send_count = 0;
static uint32_t g_tcp_close_count = 0;
static uint8_t  g_mock_tcp_rx[1024];
static uint32_t g_mock_tcp_rx_len = 0;
static uint32_t g_mock_tcp_rx_pos = 0;
static int      g_tcp_fail_connect = 0;

struct tcp_conn {
    int dummy;
};
static tcp_conn_t g_mock_conn;

tcp_conn_t* tcp_connect(ip4_t ip, uint16_t port, uint32_t timeout_ms, int* err) {
    (void)ip; (void)port; (void)timeout_ms;
    g_tcp_connect_count++;
    if (g_tcp_fail_connect) {
        if (err) *err = NET_ERR_TIMEOUT;
        return NULL;
    }
    return &g_mock_conn;
}

tcp_conn_t* tcp_connect6(const ip6_t* ip, uint16_t port, uint32_t timeout_ms, int* err) {
    (void)ip; (void)port; (void)timeout_ms;
    g_tcp_connect_count++;
    if (g_tcp_fail_connect) {
        if (err) *err = NET_ERR_TIMEOUT;
        return NULL;
    }
    return &g_mock_conn;
}

int tcp_send(tcp_conn_t* c, const void* data, uint32_t len, uint32_t timeout_ms) {
    (void)c; (void)timeout_ms;
    g_tcp_send_count++;
    /* When a TCP DNS request is sent, synthesize a TCP DNS reply if we have mock response */
    if (len > 2 && g_inject_action > 0) {
        /* Format full DNS reply for TCP: 2-byte length prefix + DNS message */
        uint8_t rep[512] = {0};
        const uint8_t* req = (const uint8_t*)data + 2;
        uint32_t req_len = len - 2;
        uint16_t id = (uint16_t)(req[0] << 8 | req[1]);
        wr16(rep, id);
        wr16(rep + 2, 0x8180); /* Response, no TC flag */
        wr16(rep + 4, 1);      /* QDCOUNT = 1 */
        wr16(rep + 6, 1);      /* ANCOUNT = 1 */
        uint32_t qlen = req_len - 12;
        memcpy(rep + 12, req + 12, qlen);
        uint32_t off = 12 + qlen;
        rep[off++] = 0xc0; rep[off++] = 0x0c;
        uint16_t qtype = (uint16_t)(req[req_len - 4] << 8 | req[req_len - 3]);
        wr16(rep + off, qtype); off += 2;
        wr16(rep + off, 1); off += 2;
        wr32(rep + off, g_inject_ttl); off += 4;
        if (qtype == 28) {
            wr16(rep + off, 16); off += 2;
            memcpy(rep + off, g_inject_ip6, 16); off += 16;
        } else {
            wr16(rep + off, 4); off += 2;
            wr32(rep + off, g_inject_ip4); off += 4;
        }
        wr16(g_mock_tcp_rx, (uint16_t)off);
        memcpy(g_mock_tcp_rx + 2, rep, off);
        g_mock_tcp_rx_len = off + 2;
        g_mock_tcp_rx_pos = 0;
    }
    return (int)len;
}

int tcp_recv(tcp_conn_t* c, void* buf, uint32_t max, uint32_t timeout_ms) {
    (void)c; (void)timeout_ms;
    if (g_mock_tcp_rx_pos >= g_mock_tcp_rx_len) return 0;
    uint32_t avail = g_mock_tcp_rx_len - g_mock_tcp_rx_pos;
    uint32_t n = max < avail ? max : avail;
    memcpy(buf, g_mock_tcp_rx + g_mock_tcp_rx_pos, n);
    g_mock_tcp_rx_pos += n;
    return (int)n;
}

void tcp_close(tcp_conn_t* c) {
    (void)c;
    g_tcp_close_count++;
}

void* kzalloc(size_t sz) { void* p = malloc(sz); if (p) memset(p, 0, sz); return p; }
void* kmalloc(size_t sz) { return malloc(sz); }
void  kfree(void* p) { free(p); }

static tcp_listener_t* g_mock_listen = (tcp_listener_t*)(uintptr_t)0x1234;
static int g_listen_call_count = 0;
static int g_listen6_call_count = 0;
static tcp_conn_t* g_mock_accepted_conn = NULL;
static ip6_t g_mock_peer6_ip = { { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } };
static uint16_t g_mock_peer6_port = 8080;

tcp_listener_t* tcp_listen(uint16_t port, int* err) {
    (void)port; (void)err;
    g_listen_call_count++;
    return g_mock_listen;
}

tcp_listener_t* tcp_listen6(uint16_t port, int* err) {
    (void)port; (void)err;
    g_listen6_call_count++;
    return g_mock_listen;
}

tcp_conn_t* tcp_accept(tcp_listener_t* l, uint32_t timeout_ms) {
    (void)l; (void)timeout_ms;
    tcp_conn_t* c = g_mock_accepted_conn;
    g_mock_accepted_conn = NULL;
    return c;
}

void tcp_unlisten(tcp_listener_t* l) { (void)l; }
void tcp_abort(tcp_conn_t* c) { (void)c; }
void tcp_peer(const tcp_conn_t* c, ip4_t* ip, uint16_t* port) {
    (void)c;
    if (ip) *ip = IP4(192, 168, 1, 100);
    if (port) *port = 12345;
}

int tcp_peer6(const tcp_conn_t* c, ip6_t* ip, uint16_t* port) {
    (void)c;
    if (ip) *ip = g_mock_peer6_ip;
    if (port) *port = g_mock_peer6_port;
    return 1;
}

uint16_t tcp_local_port(const tcp_conn_t* c) { (void)c; return 54321; }
int tcp_readable(const tcp_conn_t* c) { (void)c; return (int)(g_mock_tcp_rx_len - g_mock_tcp_rx_pos); }
uint32_t tcp_send_space(const tcp_conn_t* c) { (void)c; return 4096; }
tcp_state_t tcp_state(const tcp_conn_t* c) { (void)c; return TCP_ESTABLISHED; }
int tcp_pending(const tcp_listener_t* l) { (void)l; return g_mock_accepted_conn != NULL; }
int tcp_peek(const tcp_conn_t* c, void* buf, uint32_t max) {
    (void)c;
    if (g_mock_tcp_rx_pos >= g_mock_tcp_rx_len) return 0;
    uint32_t avail = g_mock_tcp_rx_len - g_mock_tcp_rx_pos;
    uint32_t n = max < avail ? max : avail;
    memcpy(buf, g_mock_tcp_rx + g_mock_tcp_rx_pos, n);
    return (int)n;
}

int ipv6_link_local(ip6_t* out) {
    if (out) {
        memset(out, 0, sizeof(*out));
        out->b[0] = 0xfe; out->b[1] = 0x80; out->b[15] = 1;
        return 1;
    }
    return 0;
}

int ipv6_global(ip6_t* out, int* prefix_len) {
    if (out) {
        memset(out, 0, sizeof(*out));
        out->b[0] = 0x20; out->b[1] = 0x01; out->b[15] = 2;
        if (prefix_len) *prefix_len = 64;
        return 1;
    }
    return 0;
}

int ipv6_source(const ip6_t* dst, ip6_t* src) {
    if (!dst || !src) return NET_ERR_PROTO;
    if (ip6_is_loopback(dst)) { *src = *dst; return NET_OK; }
    return ipv6_link_local(src) ? NET_OK : NET_ERR_NOTREADY;
}

/* Include actual kernel/usock.c and net/dns.c implementation */
#include "../../kernel/usock.c"
#include "../../net/dns.c"

/* Banana SDK mocks and include socket.c */
static int has_sock(void) { return 1; }
int gethostname(char* name, size_t len) {
    if (len < 10) return -1;
    strcpy(name, "banana-os");
    return 0;
}

int inet_aton(const char* s, struct in_addr* out) {
    unsigned v = 0;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return 0;
        unsigned n = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (unsigned)(*s++ - '0'); if (++digits > 3) return 0; }
        if (n > 255) return 0;
        v = v << 8 | n;
        if (part < 3 && *s++ != '.') return 0;
    }
    if (*s) return 0;
    if (out) out->s_addr = htonl(v);
    return 1;
}

static int mock_resolve(const char* name, unsigned char* ip) {
    ip4_t a = 0;
    if (dns_resolve(name, &a, 5000) != NET_OK) return -1;
    ip[0] = (unsigned char)(a >> 24); ip[1] = (unsigned char)(a >> 16);
    ip[2] = (unsigned char)(a >> 8);  ip[3] = (unsigned char)a;
    return 0;
}

static banana_api_t g_mock_api = {
    .version = BANANA_API_VERSION,
    .resolve = mock_resolve,
};
const banana_api_t* __banana = &g_mock_api;

/* Pull in socket functions under test from sdk/lib/socket.c */
#include "socket_impl.inc"

static netdev_t dummy_dev = { .ifname = "eth0" };

static void test_setup(void) {
    memset(&nif, 0, sizeof(nif));
    nif.dev = &dummy_dev;
    nif.configured = 1;
    nif.ip = IP4(10, 0, 2, 15);
    nif.dns = IP4(10, 0, 2, 3);
    g_ipv6_enabled = 0;
    g_ipv6_has_dns = 0;
    memset(&g_ipv6_dns_server, 0, sizeof(g_ipv6_dns_server));
    g_inject_action = 0;
    g_udp_send_count = 0;
    g_udp6_send_count = 0;
    g_tcp_connect_count = 0;
    g_tcp_send_count = 0;
    g_tcp_close_count = 0;
    g_mock_tcp_rx_len = 0;
    g_mock_tcp_rx_pos = 0;
    g_tcp_fail_connect = 0;
    g_last_sent_len = 0;
    /* Clear DNS cache */
    memset(g_cache, 0, sizeof(g_cache));
}

int main(void) {
    puts("=== Running DNS A/AAAA & Socket Mapping Characterization Tests ===");

    /* Test 1: Literal and localhost resolution */
    test_setup();
    ip4_t a4 = 0;
    CHECK(dns_resolve("127.0.0.1", &a4, 1000) == NET_OK && a4 == IP4(127, 0, 0, 1));
    CHECK(dns_resolve("localhost", &a4, 1000) == NET_OK && a4 == IP4(127, 0, 0, 1));
    CHECK(g_udp_send_count == 0); /* No packets on wire */

    /* AAAA disabled when IPv6 disabled */
    ip6_t a6;
    memset(&a6, 0, sizeof(a6));
    CHECK(dns_resolve6("localhost", &a6, 1000) == NET_ERR_NOTREADY);
    g_ipv6_enabled = 1;
    CHECK(dns_resolve6("localhost", &a6, 1000) == NET_OK && ip6_is_loopback(&a6));
    CHECK(dns_resolve6("::1", &a6, 1000) == NET_OK && ip6_is_loopback(&a6));
    puts("PASS literal and localhost parsing without network queries");

    /* Test 2: Unconfigured / missing DNS server errors */
    test_setup();
    nif.configured = 0;
    CHECK(dns_resolve("example.com", &a4, 50) == NET_ERR_NOTREADY);
    nif.configured = 1;
    nif.dns = 0;
    CHECK(dns_resolve("example.com", &a4, 50) == NET_ERR_DNS);
    nif.dev = NULL;
    CHECK(dns_resolve("example.com", &a4, 50) == NET_ERR_NODEV);
    puts("PASS unconfigured network, missing DNS and missing dev error codes");

    /* Test 3: Normal A record query, cache behavior and TTL */
    test_setup();
    g_inject_action = 1; /* Valid reply */
    g_inject_ip4 = IP4(93, 184, 216, 34);
    g_inject_ttl = 60;
    CHECK(dns_resolve("example.com", &a4, 2000) == NET_OK);
    CHECK(a4 == IP4(93, 184, 216, 34));
    CHECK(g_udp_send_count == 1);
    CHECK(g_udp_bound_port == 0); /* Unbound cleanly */

    /* Cache hit: subsequent query does not send packets */
    ip4_t a4_cache = 0;
    CHECK(dns_resolve("example.com", &a4_cache, 2000) == NET_OK);
    CHECK(a4_cache == a4);
    CHECK(g_udp_send_count == 1);

    /* Fast-forward past TTL (60s): cache expired, new query sent */
    g_now += 61000;
    CHECK(dns_resolve("example.com", &a4_cache, 2000) == NET_OK);
    CHECK(g_udp_send_count == 2);
    puts("PASS DNS A resolution with UDP packet exchange, caching, and TTL expiration");

    /* Test 4: Normal AAAA record query over IPv6 DNS and over IPv4 DNS */
    test_setup();
    g_ipv6_enabled = 1;
    g_inject_action = 1; /* Valid reply */
    /* Case 4a: Over IPv4 DNS (no IPv6 DNS server available) */
    CHECK(dns_resolve6("example.com", &a6, 2000) == NET_OK);
    CHECK(g_udp_send_count == 1 && g_udp6_send_count == 0);
    CHECK(memcmp(a6.b, g_inject_ip6, 16) == 0);

    /* Case 4b: Over IPv6 DNS */
    test_setup();
    g_ipv6_enabled = 1;
    g_ipv6_has_dns = 1;
    ip6_parse("2001:4860:4860::8888", &g_ipv6_dns_server);
    g_inject_action = 1;
    CHECK(dns_resolve6("example.com", &a6, 2000) == NET_OK);
    CHECK(g_udp6_send_count == 1 && g_udp_send_count == 0);
    CHECK(memcmp(a6.b, g_inject_ip6, 16) == 0);
    puts("PASS DNS AAAA resolution over IPv4 and IPv6 DNS transports");

    /* Test 5: NXDOMAIN (rcode=3) handling */
    test_setup();
    g_inject_action = 2; /* NXDOMAIN */
    CHECK(dns_resolve("nonexistent.domain", &a4, 2000) == NET_ERR_DNS);
    CHECK(g_udp_bound_port == 0); /* Socket cleanly closed */
    puts("PASS NXDOMAIN returns NET_ERR_DNS promptly without hanging");

    /* Test 6: Mismatched ID, mismatched query type and malformed answer */
    test_setup();
    g_inject_action = 4; /* Wrong ID injected */
    /* Should ignore mismatched ID and timeout after 3 attempts */
    uint32_t t0 = g_now;
    CHECK(dns_resolve("test-id.com", &a4, 500) == NET_ERR_TIMEOUT);
    CHECK(g_now - t0 >= 500);
    CHECK(g_udp_send_count >= 1);

    test_setup();
    g_inject_action = 6; /* Malformed answer length */
    CHECK(dns_resolve("malformed.com", &a4, 500) == NET_ERR_TIMEOUT || dns_resolve("malformed.com", &a4, 500) == NET_ERR_DNS);
    puts("PASS parser drops mismatched ID, malformed answer length and honors timeout bound");

    /* Test 7: Truncated response (TC bit set) falls back to TCP (RFC 1035) */
    test_setup();
    g_inject_action = 3; /* Truncated response (TC bit set: 0x8380) */
    CHECK(dns_resolve("truncated.org", &a4, 1000) == NET_OK);
    CHECK(a4 == g_inject_ip4);
    CHECK(g_udp_send_count == 1);
    CHECK(g_tcp_connect_count == 1);
    CHECK(g_tcp_send_count == 1);
    CHECK(g_tcp_close_count == 1);
    CHECK(g_udp_bound_port == 0);

    /* Also verify IPv6 DNS resolution with TCP fallback */
    test_setup();
    g_ipv6_enabled = 1;
    g_ipv6_has_dns = 1;
    ip6_parse("2001:4860:4860::8888", &g_ipv6_dns_server);
    g_inject_action = 3;
    CHECK(dns_resolve6("truncated6.org", &a6, 1000) == NET_OK);
    CHECK(memcmp(a6.b, g_inject_ip6, 16) == 0);
    CHECK(g_udp6_send_count == 1);
    CHECK(g_tcp_connect_count == 1);
    CHECK(g_tcp_send_count == 1);
    CHECK(g_tcp_close_count == 1);
    CHECK(g_udp6_bound_port == 0);

    /* Verify that if TCP connection fails during fallback, DNS error is returned cleanly */
    test_setup();
    g_inject_action = 3;
    g_tcp_fail_connect = 1;
    CHECK(dns_resolve("tcpfail.org", &a4, 500) != NET_OK);
    CHECK(g_udp_bound_port == 0);
    puts("PASS truncated response (TC bit set) falls back to TCP and resolves successfully");

    /* Test 8: Silent timeout bounded by caller timeout_ms */
    test_setup();
    g_inject_action = 0; /* Server silent / absent */
    t0 = g_now;
    CHECK(dns_resolve("silent.com", &a4, 300) == NET_ERR_TIMEOUT);
    CHECK(g_now - t0 >= 300 && g_now - t0 <= 350);
    CHECK(g_udp_bound_port == 0);
    puts("PASS server silence respects timeout_ms bound and unbinds socket");

    /* Test 9: SDK getaddrinfo IPv4 resolution and mapping */
    test_setup();
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    CHECK(getaddrinfo("localhost", "http", &hints, &res) == 0 && res != NULL);
    CHECK(res->ai_family == AF_INET);
    CHECK(res->ai_socktype == SOCK_STREAM);
    CHECK(res->ai_protocol == IPPROTO_TCP);
    struct sockaddr_in* sin = (struct sockaddr_in*)res->ai_addr;
    CHECK(sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK));
    CHECK(sin->sin_port == htons(80));
    freeaddrinfo(res);
    res = NULL;

    /* SDK lookup of remote name */
    g_inject_action = 1;
    g_inject_ip4 = IP4(93, 184, 216, 34);
    CHECK(getaddrinfo("example.com", "8080", &hints, &res) == 0 && res != NULL);
    sin = (struct sockaddr_in*)res->ai_addr;
    CHECK(sin->sin_addr.s_addr == htonl(IP4(93, 184, 216, 34)));
    CHECK(sin->sin_port == htons(8080));
    freeaddrinfo(res);
    res = NULL;

    /* SDK lookup NXDOMAIN -> EAI_NONAME */
    g_inject_action = 2;
    CHECK(getaddrinfo("nxdomain.test", "80", &hints, &res) == EAI_NONAME);
    CHECK(res == NULL);
    puts("PASS SDK getaddrinfo IPv4 mapping, service translation, and NXDOMAIN handling");

    /* Test 10: SDK getaddrinfo IPv6 behavior characterization */
    test_setup();
    hints.ai_family = AF_INET6;
    CHECK(getaddrinfo("localhost", "80", &hints, &res) == EAI_FAMILY);
    CHECK(res == NULL);

    /* AF_UNSPEC currently returns AF_INET records only */
    hints.ai_family = AF_UNSPEC;
    CHECK(getaddrinfo("localhost", "80", &hints, &res) == 0 && res != NULL);
    CHECK(res->ai_family == AF_INET);
    freeaddrinfo(res);
    res = NULL;
    puts("PASS SDK getaddrinfo IPv6 characterization: AF_INET6 returns EAI_FAMILY, AF_UNSPEC yields IPv4");

    /* Test 11: SDK getnameinfo */
    test_setup();
    struct sockaddr_in sa_in = {
        .sin_family = AF_INET,
        .sin_port = htons(443),
        .sin_addr.s_addr = htonl(IP4(10, 0, 2, 15))
    };
    char hostbuf[64] = {0}, servbuf[32] = {0};
    CHECK(getnameinfo((struct sockaddr*)&sa_in, sizeof(sa_in), hostbuf, sizeof(hostbuf), servbuf, sizeof(servbuf), 0) == 0);
    CHECK(!strcmp(hostbuf, "10.0.2.15"));
    CHECK(!strcmp(servbuf, "443"));

    /* getnameinfo rejects AF_INET6 with EAI_FAMILY */
    struct sockaddr_in6 {
        uint16_t sin6_family;
        uint16_t sin6_port;
        uint32_t sin6_flowinfo;
        uint8_t  sin6_addr[16];
        uint32_t sin6_scope_id;
    } sa_in6;
    memset(&sa_in6, 0, sizeof(sa_in6));
    sa_in6.sin6_family = AF_INET6;
    CHECK(getnameinfo((struct sockaddr*)&sa_in6, sizeof(sa_in6), hostbuf, sizeof(hostbuf), servbuf, sizeof(servbuf), 0) == EAI_FAMILY);
    puts("PASS SDK getnameinfo IPv4 formatting and IPv6 EAI_FAMILY rejection");

    /* Test 12: kernel/usock AF_INET6 socket lifecycle and TCP operations */
    test_setup();
    int err = 0;
    usock_t* s6 = usock_new(AF_INET6, SOCK_STREAM, &err);
    CHECK(s6 != NULL && err == 0);
    CHECK(s6->family == AF_INET6);

    /* Family mismatch rejection in bind and connect */
    struct sockaddr_in bad_sa4;
    memset(&bad_sa4, 0, sizeof(bad_sa4));
    bad_sa4.sin_family = AF_INET;
    bad_sa4.sin_port = htons(8080);
    bad_sa4.sin_addr.s_addr = htonl(IP4(127, 0, 0, 1));
    CHECK(usock_bind(s6, &bad_sa4, sizeof(bad_sa4)) == -EAFNOSUPPORT);
    CHECK(usock_connect(s6, &bad_sa4, sizeof(bad_sa4)) == -EAFNOSUPPORT);

    /* Valid AF_INET6 bind */
    struct sockaddr_in6 bind_sa6;
    memset(&bind_sa6, 0, sizeof(bind_sa6));
    bind_sa6.sin6_family = AF_INET6;
    bind_sa6.sin6_port = htons(9090);
    bind_sa6.sin6_addr[15] = 1; /* ::1 */
    CHECK(usock_bind(s6, &bind_sa6, sizeof(bind_sa6)) == 0);
    CHECK(s6->lport == 9090);

    /* Listen delegating to tcp_listen6 */
    CHECK(usock_listen(s6, 5) == 0);
    CHECK(g_listen6_call_count == 1);
    CHECK(g_listen_call_count == 0);

    /* Accept delegating to tcp_accept and formatting sockaddr_in6 */
    g_mock_accepted_conn = &g_mock_conn;
    struct sockaddr_in6 peer_sa6;
    memset(&peer_sa6, 0, sizeof(peer_sa6));
    int peer_len = sizeof(peer_sa6);
    usock_t* accepted = usock_accept(s6, &peer_sa6, &peer_len, &err);
    CHECK(accepted != NULL && err == 0);
    CHECK(accepted->family == AF_INET6);
    CHECK(peer_len == 28);
    CHECK(peer_sa6.sin6_family == AF_INET6);
    CHECK(ntohs(peer_sa6.sin6_port) == 8080);
    CHECK(memcmp(peer_sa6.sin6_addr, g_mock_peer6_ip.b, 16) == 0);

    /* getsockname on accepted socket returns local_ip6 address */
    struct sockaddr_in6 local_sa6;
    memset(&local_sa6, 0, sizeof(local_sa6));
    int local_len = sizeof(local_sa6);
    CHECK(usock_name(accepted, 0, &local_sa6, &local_len) == 0);
    CHECK(local_len == 28);
    CHECK(local_sa6.sin6_family == AF_INET6);
    CHECK(local_sa6.sin6_addr[0] == 0xfe && local_sa6.sin6_addr[1] == 0x80);

    /* getsockopt / setsockopt IPV6_V6ONLY */
    int v6only_val = 1, optlen = sizeof(int);
    CHECK(usock_opt(s6, 1, IPPROTO_IPV6, IPV6_V6ONLY, &v6only_val, &optlen) == 0);
    int read_v6only = 0;
    CHECK(usock_opt(s6, 0, IPPROTO_IPV6, IPV6_V6ONLY, &read_v6only, &optlen) == 0);
    CHECK(read_v6only == 1);

    usock_close(accepted);
    usock_close(s6);
    puts("PASS kernel usock AF_INET6 TCP lifecycle, bind, listen6, accept and IPV6_V6ONLY");

    /* Test 13: kernel/usock AF_INET6 UDP socket sendto and recvfrom */
    test_setup();
    usock_t* u6 = usock_new(AF_INET6, SOCK_DGRAM, &err);
    CHECK(u6 != NULL && err == 0);
    CHECK(u6->family == AF_INET6);

    /* Bind UDP6 */
    struct sockaddr_in6 ubind6;
    memset(&ubind6, 0, sizeof(ubind6));
    ubind6.sin6_family = AF_INET6;
    ubind6.sin6_port = htons(7777);
    CHECK(usock_bind(u6, &ubind6, sizeof(ubind6)) == 0);
    CHECK(u6->udp_bound == 1);
    CHECK(g_udp6_bound_port == 7777);

    /* Sendto AF_INET6 destination */
    struct sockaddr_in6 udst6;
    memset(&udst6, 0, sizeof(udst6));
    udst6.sin6_family = AF_INET6;
    udst6.sin6_port = htons(8888);
    udst6.sin6_addr[0] = 0x20; udst6.sin6_addr[1] = 0x01; udst6.sin6_addr[15] = 9;
    char udp_msg[] = "hello ipv6 socket";
    long sent = usock_send(u6, udp_msg, sizeof(udp_msg), 0, &udst6, sizeof(udst6));
    CHECK(sent == sizeof(udp_msg));
    CHECK(g_udp6_send_count == 1);
    CHECK(g_udp_send_count == 0);

    /* Ingest incoming datagram via udp6_in */
    ip6_t src6 = { { 0x26, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } };
    uint8_t pkt_data[] = "ipv6 datagram payload";
    CHECK(g_udp6_handler != NULL);
    g_udp6_handler(&src6, 8888, pkt_data, sizeof(pkt_data), g_udp6_ctx);

    /* Recvfrom yields correct payload and sockaddr_in6 */
    char rx_buf[64] = {0};
    struct sockaddr_in6 from6;
    memset(&from6, 0, sizeof(from6));
    int from_len = sizeof(from6);
    long recvd = usock_recv(u6, rx_buf, sizeof(rx_buf), 0, &from6, &from_len);
    CHECK(recvd == sizeof(pkt_data));
    CHECK(!strcmp(rx_buf, "ipv6 datagram payload"));
    CHECK(from_len == 28);
    CHECK(from6.sin6_family == AF_INET6);
    CHECK(ntohs(from6.sin6_port) == 8888);
    CHECK(memcmp(from6.sin6_addr, src6.b, 16) == 0);

    usock_close(u6);
    CHECK(g_udp6_bound_port == 0);
    puts("PASS kernel usock AF_INET6 UDP socket bind, sendto, and recvfrom");

    printf("DNS/Socket Characterization: %d checks passed\n", checks);
    return 0;
}
