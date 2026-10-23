/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the real DHCP client using UDP, clock and device shims. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "net.h"
#include "kstring.h"

static netif_t nif;
static uint32_t now, xid, sends, flushes;
static ip4_t last_dst;
static uint8_t sent[384];
static udp_handler_t handler;
static void* handler_ctx;
static int carrier = 1, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
netif_t* net_if(void) { return &nif; }
uint32_t timer_ms(void) { return now; }
uint32_t random_u32(void) { return ++xid; }
void klog(const char* fmt, ...) { (void)fmt; }
void arp_request(ip4_t ip) { (void)ip; }
void arp_flush(void) { flushes++; }
void ip4_to_str(ip4_t ip, char out[16]) {
    snprintf(out, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}
int udp_bind(uint16_t port, udp_handler_t h, void* ctx) {
    CHECK(port == 68); handler = h; handler_ctx = ctx; return 0;
}
void udp_unbind(uint16_t port) { CHECK(port == 68); handler = NULL; }
int udp_send(ip4_t dst, uint16_t sport, uint16_t dport, const void* p, uint32_t len) {
    CHECK((dst == IP4_BROADCAST || dst == IP4(10, 0, 2, 2)) && sport == 68 && dport == 67 && len <= sizeof(sent));
    last_dst = dst;
    memset(sent, 0, sizeof(sent)); memcpy(sent, p, len); sends++; return 0;
}
static int link_up(netdev_t* nd) { (void)nd; return carrier; }
static netdev_t dev = {.ifname = "eth0", .mac = {2, 3, 4, 5, 6, 7}, .link_up = link_up};
static void state(const char* expected) { CHECK(!strcmp(dhcp_state_str(), expected)); }
static void tick(uint32_t delta) { now += delta; dhcp_timer(); }
static void reply_opts(uint8_t type, uint32_t lease, uint32_t t1, uint32_t t2) {
    uint8_t p[300] = {0};
    p[0] = 2; p[1] = 1; p[2] = 6;
    memcpy(p + 4, sent + 4, 4); memcpy(p + 28, dev.mac, 6);
    wr32(p + 16, IP4(10, 0, 2, 15)); wr32(p + 236, 0x63825363u);
    uint8_t* o = p + 240;
    *o++ = 53; *o++ = 1; *o++ = type;
    *o++ = 51; *o++ = 4; wr32(o, lease); o += 4;
    *o++ = 54; *o++ = 4; wr32(o, IP4(10, 0, 2, 2)); o += 4;
    if (t1) { *o++ = 58; *o++ = 4; wr32(o, t1); o += 4; }
    if (t2) { *o++ = 59; *o++ = 4; wr32(o, t2); o += 4; }
    *o++ = 255;
    CHECK(handler != NULL); handler(IP4(10, 0, 2, 2), 67, p, (uint32_t)(o - p), handler_ctx);
}
static void reply(uint8_t type, uint32_t lease) {
    reply_opts(type, lease, 0, 0);
}
static void reset(uint32_t start) {
    dhcp_stop(); memset(&nif, 0, sizeof(nif)); nif.dev = &dev;
    now = start; sends = 0; carrier = 1; dhcp_start();
    state("discovering"); CHECK(!nif.configured && nif.dhcp && sends == 1);
}
static void acquire_opts(uint32_t lease, uint32_t t1, uint32_t t2) {
    reply_opts(2, lease, t1, t2); state("requesting"); CHECK(sent[242] == 3);
    reply_opts(5, lease, t1, t2); state("bound"); CHECK(nif.configured && nif.lease_s == (lease ? lease : 3600));
}
static void acquire(uint32_t lease) {
    acquire_opts(lease, 0, 0);
}

static const char* host = "my-banana";
const char* hostname_get(void) { return host; }

int main(void) {
    reset(0);
    tick(999); CHECK(sends == 1); tick(1); CHECK(sends == 2);
    tick(2000); CHECK(sends == 3); tick(4000); CHECK(sends == 4);
    tick(8000); CHECK(sends == 5); tick(16000); CHECK(sends == 6);
    tick(16000); CHECK(sends == 7); state("discovering");
    puts("PASS discovery retries at 0, 1, 3, 7, 15, 31, 47 seconds; no acquisition deadline");

    reset(100); reply(2, 120); state("requesting");
    uint32_t request_xid = rd32(sent + 4);
    tick(1000); tick(2000); tick(4000); state("requesting");
    tick(7999); state("requesting"); tick(1); state("discovering");
    CHECK(rd32(sent + 4) != request_xid && !nif.configured);
    puts("PASS unanswered initial REQUEST restarts discovery after 15 seconds");

    reset(0); acquire(120);
    CHECK(nif.bound_ms == 0 && nif.ip == IP4(10, 0, 2, 15));
    tick(59999); state("bound"); tick(1); state("renewing");
    CHECK(rd32(sent + 12) == nif.ip);
    tick(2000); reply(5, 120); state("bound"); CHECK(nif.bound_ms == 62000);
    tick(59999); state("bound"); tick(1); state("renewing");
    puts("PASS half-lease renewal and ACK reset the renewal deadline");

    /* T1 default is lease / 2 = 60s, T2 default is 7/8 * 120s = 105s, expiry is 120s */
    /* At 60s, enter renewing. Retry at +1s, +2s, +4s, +8s, +16s, +14s -> at 105s transitions to rebinding */
    tick(1000); CHECK(sent[242] == 3); /* retry in renewing */
    tick(44000); state("rebinding");
    CHECK(nif.configured);
    /* In rebinding, broadcast REQUEST without server-id; when 120s lease expires, address is invalidated */
    tick(14999); state("rebinding"); CHECK(nif.configured);
    tick(1); state("discovering");
    CHECK(!nif.configured);
    puts("PASS renewal transitions to rebinding at T2 and expires at lease end, clearing configured");

    reply(2, 120); reply(6, 120); state("discovering"); CHECK(!nif.configured);
    acquire(120); tick(60000); state("renewing"); reply(6, 120); state("discovering"); CHECK(!nif.configured);
    acquire(120); tick(60000); state("renewing"); tick(45000); state("rebinding"); reply(6, 120); state("discovering"); CHECK(!nif.configured);
    puts("PASS NAK clears configured in requesting, renewing and rebinding states");

    /* Short 4-second lease: T1=2s, T2=3s, expiry=4s */
    reset(0); acquire(4);
    tick(1999); state("bound"); CHECK(nif.configured);
    tick(1); state("renewing"); CHECK(nif.configured);
    tick(1500); state("rebinding"); CHECK(nif.configured);
    tick(499); state("rebinding"); CHECK(nif.configured);
    tick(1); state("discovering"); CHECK(!nif.configured);

    puts("PASS 4-second lease enters renewing at 2s, rebinding at 3.5s and invalidates at 4s expiry");

    /* Explicit T1 and T2 options */
    reset(0); acquire_opts(100, 20, 80);
    tick(19999); state("bound"); tick(1); state("renewing");
    tick(59999); state("renewing"); tick(1); state("rebinding");
    tick(19999); state("rebinding"); tick(1); state("discovering"); CHECK(!nif.configured);
    puts("PASS explicit T1 and T2 options respected");

    /* Infinite lease (0xFFFFFFFF): stays bound, never renews or expires */
    reset(0); acquire(0xFFFFFFFFu);
    tick(86400000); state("bound"); CHECK(nif.configured);
    tick(86400000); state("bound"); CHECK(nif.configured);
    puts("PASS infinite lease (0xFFFFFFFF) remains bound indefinitely without renewal or expiry");

    reset(0); acquire(0); tick(1799999); state("bound"); tick(1); state("renewing");
    puts("PASS absent/zero lease defaults to 3600 seconds");

    reset(0xfffffff0u); tick(999); CHECK(sends == 1); tick(1); CHECK(sends == 2);
    reset(0xfffffff0u); acquire(120); tick(59999); state("bound"); tick(1); state("renewing");
    puts("PASS retry and renewal deadlines across uint32 millisecond wrap");

    /* Packets preserve upstream's configurable host name, including its maximum length. */
    reset(0);
    int seen_host = 0, seen_timers = 0;
    for (uint32_t i = 240; sent[i] != 255; i += 2u + sent[i + 1]) {
        if (sent[i] == 12) {
            CHECK(sent[i+1] == strlen(host));
            CHECK(!memcmp(sent+i+2, host, strlen(host))); seen_host = 1;
        }
        if (sent[i] == 55) { CHECK(sent[i+1] == 6 && sent[i+6] == 58 && sent[i+7] == 59); seen_timers = 1; }
    }
    CHECK(seen_host && seen_timers);
    host = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijk";
    reset(0); CHECK(strlen(host) == 63);
    acquire(120); tick(60000);
    CHECK(last_dst == nif.dhcp_server && rd16(sent + 10) == 0);
    uint32_t renew_xid = rd32(sent + 4);
    tick(45000); CHECK(last_dst == IP4_BROADCAST && rd16(sent + 10) == 0x8000);
    CHECK(rd32(sent + 4) == renew_xid && rd32(sent + 12) == nif.ip);
    uint32_t old_flushes = flushes; tick(15000); CHECK(flushes == old_flushes + 1 && !nif.configured);
    puts("PASS configurable host name, unicast renewal, broadcast rebinding, XID and ARP invalidation");

    reset(0); acquire(1); tick(499); state("bound"); tick(1); state("renewing");
    tick(375); state("rebinding"); tick(125); state("discovering"); CHECK(!nif.configured);
    reset(0); acquire_opts(100, 95, 80); tick(49999); state("bound");
    tick(1); state("renewing"); tick(37500); state("rebinding"); tick(12500); state("discovering");
    puts("PASS one-second leases and invalid T1/T2 pairs");

    /* A lease spanning multiple uint32 millisecond wraps (100 days). */
    reset(0xfffffff0u); acquire(8640000);
    for (int day = 0; day < 49; day++) { tick(86400000); state("bound"); }
    tick(86400000); state("renewing");
    for (int day = 0; day < 37; day++) { tick(86400000); state("renewing"); }
    tick(43200000); state("rebinding");
    for (int day = 0; day < 12; day++) { tick(86400000); state("rebinding"); }
    tick(43200000); state("discovering"); CHECK(!nif.configured);
    reset(0); acquire(0xfffffffeu); tick(86400000); state("bound");
    reset(0); acquire(120); tick(110000); state("rebinding"); /* late timer skips straight to T2 */
    tick(10000); state("discovering");
    puts("PASS long finite leases, multiple timer wraps and overdue timer ticks");

    dhcp_stop(); state("idle"); CHECK(!handler); uint32_t old = sends;
    tick(60000); CHECK(sends == old);
    nif.dhcp = 0;
    puts("PASS stop cancels timers and UDP listener");
    printf("DHCP: %d checks passed\n", checks);
    return 0;
}
