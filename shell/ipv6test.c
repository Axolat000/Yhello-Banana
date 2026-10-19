/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ipv6test.h"
#include "../net/ipv6.h"
#include "../net/tcp.h"
#include "../net/http.h"
#include "../kernel/kstring.h"
#include "../kernel/timer.h"
#include "../kernel/kheap.h"

static int g_running;
typedef struct { int count; uint16_t port; uint32_t len; uint8_t data[64]; } received_t;

static void receive6(const ip6_t* src, uint16_t port, const uint8_t* data, uint32_t len, void* ctx) {
    received_t* r = ctx;
    if (!ip6_is_loopback(src)) return;
    r->count++; r->port = port; r->len = len;
    if (len <= sizeof(r->data)) memcpy(r->data, data, len);
}

static int bind_test_port(received_t* received, uint16_t* out) {
    for (int i = 0; i < 32; i++) {
        uint16_t p = udp6_ephemeral_port();
        if (p && udp6_bind(p, receive6, received) == NET_OK) { *out = p; return 1; }
    }
    return 0;
}

static void pump(void) { for (int i = 0; i < 4; i++) net_poll(); }

void ipv6test_run(const char* host, uint16_t port,
                 void (*report)(const char*, int, const char*)) {
    if (g_running) { report("IPv6 tests", 0, "already running in another terminal"); return; }
    g_running = 1;
    int was_on = ipv6_enabled();
    ip6_t loop, out;
    ip6_parse("::1", &loop);
    if (!was_on) {
        int err = NET_OK;
        tcp_conn_t* c = tcp_connect6(&loop, 9, 1, &err);
        int ok = !c && err == NET_ERR_NOTREADY &&
                 udp6_send(&loop, 1, 2, "off", 3) == NET_ERR_NOTREADY &&
                 dns_resolve6("localhost", &out, 1) == NET_ERR_NOTREADY;
        if (c) tcp_abort(c);
        report("IPv6 off blocks TCP, UDP and IPv6 resolution", ok, "");
        ipv6_set_runtime(1); /* no settings write */
    }
    static const char* valid[] = { "::", "::1", "fe80::1234", "2001:db8::dead:beef", "1:2:3:4:5:6:7:8" };
    static const char* invalid[] = { "", "1:2:3", ":::", "1::2::3", "12345::", "1:2:3:4:5:6:7:8:9", "::g", "1:2:3:4:5:6:7:8:" };
    int ok = 1;
    for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        ip6_t a, b; char text[48];
        if (!ip6_parse(valid[i], &a)) { ok = 0; continue; }
        ip6_format(&a, text, sizeof(text));
        if (!ip6_parse(text, &b) || memcmp(&a, &b, 16)) ok = 0;
    }
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        if (ip6_parse(invalid[i], &out)) ok = 0;
    report("IPv6 address parsing and formatting", ok, "");
    report("IPv6 localhost resolution", dns_resolve6("localhost", &out, 1) == NET_OK &&
           ip6_is_loopback(&out), "");
    url_t* url = kmalloc(sizeof(url_t));
    ok = url && url_parse("http://[::1]:8080/a?b=c", url) &&
         !strcmp(url->host, "::1") && url->port == 8080 && !strcmp(url->path, "/a?b=c") &&
         !url_parse("http://[::1", url) && !url_parse("http://[not-an-address]/", url) &&
         !url_parse("http://::1/", url);
    kfree(url);
    report("bracketed IPv6 HTTP URLs", ok, "");

    received_t r = { 0 };
    uint16_t udp_port = 0;
    if (bind_test_port(&r, &udp_port)) {
        static const uint8_t payload[] = { 0, 1, 2, 0xFF, 4 }; /* odd length */
        int rc = udp6_send(&loop, 32123, udp_port, payload, sizeof(payload));
        pump();
        report("IPv6 UDP loopback (binary, odd length)", rc == NET_OK && r.count == 1 &&
               r.port == 32123 && r.len == sizeof(payload) && !memcmp(r.data, payload, sizeof(payload)), "");
        uint8_t d[13] = { 0 };
        wr16(d, 32123); wr16(d + 2, udp_port); wr16(d + 4, sizeof(d));
        memcpy(d + 8, payload, sizeof(payload));
        int before = r.count;
        udp6_rx(&loop, &loop, d, sizeof(d)); /* missing checksum */
        uint16_t sum = ip6_checksum(&loop, &loop, IP_PROTO_UDP, d, sizeof(d));
        wr16(d + 6, sum ? sum : 0xFFFF);
        d[12] ^= 1; udp6_rx(&loop, &loop, d, sizeof(d)); d[12] ^= 1;
        udp6_rx(&loop, &loop, d, 7); /* truncated header */
        report("UDP rejects missing/corrupt checksums and truncation", r.count == before, "");
        uint8_t ext[21] = { IP_PROTO_UDP, 0 };
        memcpy(ext + 8, d, sizeof(d));
        rc = ipv6_send(&loop, &loop, 60, ext, sizeof(ext));
        pump();
        report("IPv6 destination-options header dispatch", rc == NET_OK && r.count == before + 1, "");
        before = r.count;
        ext[1] = 255; /* claims an extension larger than the packet */
        ipv6_send(&loop, &loop, 60, ext, sizeof(ext)); pump();
        ext[1] = 0; wr16(ext + 2, 1); /* M flag: actual fragment, not atomic */
        ipv6_send(&loop, &loop, 44, ext, sizeof(ext)); pump();
        report("malformed extensions and unreassembled fragments are dropped", r.count == before, "");
        udp6_unbind(udp_port);
    } else report("IPv6 UDP tests", 0, "no free UDP binding");

    int err = NET_OK;
    tcp_listener_t* listener = NULL;
    uint16_t tcp_port = 0;
    for (int i = 0; i < 32 && !listener; i++) {
        tcp_port = udp6_ephemeral_port();
        listener = tcp_listen6(tcp_port, &err);
        if (err == NET_ERR_NOMEM) break;
    }
    tcp_conn_t* client = listener ? tcp_connect6(&loop, tcp_port, 2000, &err) : NULL;
    tcp_conn_t* server = client ? tcp_accept(listener, 1000) : NULL;
    report("IPv6 TCP connect, listen and accept", client && server, "");
    if (client && server) {
        uint8_t* tx = kmalloc(8193);
        uint8_t* rx = kmalloc(8193);
        ok = tx && rx;
        if (ok) {
            for (int i = 0; i < 8193; i++) tx[i] = (uint8_t)(i * 37 + (i >> 8));
            ok = tcp_send(client, tx, 8193, 2000) == 8193;
            uint32_t n = 0;
            while (ok && n < 8193) {
                int got = tcp_recv(server, rx + n, 8193 - n, 2000);
                if (got <= 0) ok = 0; else n += (uint32_t)got;
            }
            ok = ok && !memcmp(tx, rx, 8193);
        }
        report("TCP transfers 8193 bytes across multiple IPv6 segments", ok, "");
        char reply[5];
        ok = tcp_send(server, "pong", 4, 1000) == 4 && tcp_recv(client, reply, 4, 1000) == 4 && !memcmp(reply, "pong", 4);
        report("IPv6 TCP reverse direction", ok, "");
        kfree(tx); kfree(rx);
        tcp_close(client); client = NULL;
        report("IPv6 TCP orderly close", tcp_recv(server, reply, sizeof(reply), 2000) == 0, "");
    }
    if (client) tcp_abort(client);
    if (server) tcp_abort(server);
    if (listener) tcp_unlisten(listener);
    pump();

    /* Regression: IPv4 remains usable even when IPv6 is enabled. */
    ip4_t ip4;
    report("IPv4 localhost resolution remains independent", dns_resolve("localhost", &ip4, 1) == NET_OK && ip4 == IP4(127,0,0,1), "");
    if (host) {
        uint32_t start = timer_ms();
        int rc = NET_ERR_NOTREADY;
        do {
            rc = dns_resolve6(host, &out, 3000);
            if (rc == NET_OK || rc == NET_ERR_INTR) break;
            net_wait(100);
        } while (timer_ms() - start < 5000);
        char note[96] = "";
        if (rc == NET_OK) ip6_format(&out, note, sizeof(note));
        else kstrlcpy(note, net_strerror(rc), sizeof(note));
        report("network AAAA/address resolution", rc == NET_OK, note);
        if (rc == NET_OK) {
            client = tcp_connect6(&out, port, 5000, &err);
            report("TCP connection over the IPv6 network", client != NULL, client ? "" : net_strerror(err));
            if (client) tcp_abort(client);
        }
    }
    if (!was_on) ipv6_set_runtime(0);
    report("IPv6 toggle restored to its previous state", ipv6_enabled() == was_on,
           was_on ? "on; saved setting unchanged" : "off; saved setting unchanged");
    g_running = 0;
}
