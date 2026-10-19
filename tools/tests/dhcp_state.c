/* Characterization of the real DHCP client, including known missing behavior. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "net.h"
#include "kstring.h"

static netif_t nif;
static uint32_t now, xid, sends;
static uint8_t sent[300];
static udp_handler_t handler;
static void* handler_ctx;
static int carrier = 1, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
netif_t* net_if(void) { return &nif; }
uint32_t timer_ms(void) { return now; }
uint32_t random_u32(void) { return ++xid; }
void klog(const char* fmt, ...) { (void)fmt; }
void arp_request(ip4_t ip) { (void)ip; }
void ip4_to_str(ip4_t ip, char out[16]) {
    snprintf(out, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}
int udp_bind(uint16_t port, udp_handler_t h, void* ctx) {
    CHECK(port == 68); handler = h; handler_ctx = ctx; return 0;
}
void udp_unbind(uint16_t port) { CHECK(port == 68); handler = NULL; }
int udp_send(ip4_t dst, uint16_t sport, uint16_t dport, const void* p, uint32_t len) {
    CHECK(dst == IP4_BROADCAST && sport == 68 && dport == 67 && len <= sizeof(sent));
    memset(sent, 0, sizeof(sent)); memcpy(sent, p, len); sends++; return 0;
}
static int link_up(netdev_t* nd) { (void)nd; return carrier; }
static netdev_t dev = {.ifname = "eth0", .mac = {2, 3, 4, 5, 6, 7}, .link_up = link_up};
static void state(const char* expected) { CHECK(!strcmp(dhcp_state_str(), expected)); }
static void tick(uint32_t delta) { now += delta; dhcp_timer(); }
static void reply(uint8_t type, uint32_t lease) {
    uint8_t p[300] = {0};
    p[0] = 2; p[1] = 1; p[2] = 6;
    memcpy(p + 4, sent + 4, 4); memcpy(p + 28, dev.mac, 6);
    wr32(p + 16, IP4(10, 0, 2, 15)); wr32(p + 236, 0x63825363u);
    p[240] = 53; p[241] = 1; p[242] = type;
    p[243] = 51; p[244] = 4; wr32(p + 245, lease);
    p[249] = 54; p[250] = 4; wr32(p + 251, IP4(10, 0, 2, 2)); p[255] = 255;
    CHECK(handler != NULL); handler(IP4(10, 0, 2, 2), 67, p, 256, handler_ctx);
}
static void reset(uint32_t start) {
    dhcp_stop(); memset(&nif, 0, sizeof(nif)); nif.dev = &dev;
    now = start; sends = 0; carrier = 1; dhcp_start();
    state("discovering"); CHECK(!nif.configured && nif.dhcp && sends == 1);
}
static void acquire(uint32_t lease) {
    reply(2, lease); state("requesting"); CHECK(sent[242] == 3);
    reply(5, lease); state("bound"); CHECK(nif.configured && nif.lease_s == (lease ? lease : 3600));
}

/* Compile the unmodified draw_network body with label/button capture shims. */
#define C_HEAD 1u
#define C_DIM 2u
#define C_TEXT 3u
static char labels[300][96];
static uint32_t colors[300];
static int cx0(void) { return 0; }
static int cy0(void) { return 0; }
static void label(int x, int y, const char* text, uint32_t color) {
    (void)x; CHECK(y >= 0 && y < 300); snprintf(labels[y], sizeof(labels[y]), "%s", text); colors[y] = color;
}
static void button(int x, int y, int w, const char* text, int on) {
    (void)x; (void)y; (void)w; (void)text; (void)on;
}
int fw_enabled(void) { return 0; }
int ipv6_enabled(void) { return 0; }
int ksnprintf(char* buf, size_t n, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, n, fmt, ap); va_end(ap); return r;
}
size_t kstrlcpy(char* dst, const char* src, size_t n) {
    size_t len = strlen(src); if (n) { size_t c = len < n - 1 ? len : n - 1; memcpy(dst, src, c); dst[c] = 0; } return len;
}
#include "draw_network.inc"
static void ui_mode(const char* expected) { draw_network(); CHECK(!strcmp(labels[60], expected)); }

int main(void) {
    reset(0); ui_mode("Mode: DHCP (discovering)");
    CHECK(!strcmp(labels[42], "Link: up; address not configured"));
    tick(999); CHECK(sends == 1); tick(1); CHECK(sends == 2);
    tick(2000); CHECK(sends == 3); tick(4000); CHECK(sends == 4);
    tick(8000); CHECK(sends == 5); tick(16000); CHECK(sends == 6);
    tick(16000); CHECK(sends == 7); state("discovering");
    puts("PASS discovery retries at 0, 1, 3, 7, 15, 31, 47 seconds; no acquisition deadline");

    reset(100); reply(2, 120); state("requesting"); ui_mode("Mode: DHCP (requesting)");
    uint32_t request_xid = rd32(sent + 4);
    tick(1000); tick(2000); tick(4000); state("requesting");
    tick(7999); state("requesting"); tick(1); state("discovering");
    CHECK(rd32(sent + 4) != request_xid && !nif.configured);
    puts("PASS unanswered initial REQUEST restarts discovery after 15 seconds");

    reset(0); acquire(120); ui_mode("Mode: DHCP (bound)");
    CHECK(nif.bound_ms == 0 && nif.ip == IP4(10, 0, 2, 15));
    tick(59999); state("bound"); tick(1); state("renewing");
    CHECK(rd32(sent + 12) == nif.ip); ui_mode("Mode: DHCP (renewing)");
    CHECK(!strcmp(labels[42], "Link: up; address configured"));
    tick(2000); reply(5, 120); state("bound"); CHECK(nif.bound_ms == 62000);
    tick(59999); state("bound"); tick(1); state("renewing");
    puts("PASS half-lease renewal and ACK reset the renewal deadline");

    tick(2000); tick(4000); tick(8000); tick(16000); state("renewing");
    tick(31999); state("renewing"); tick(1); state("discovering");
    CHECK(nif.configured); ui_mode("Mode: DHCP (discovering)");
    puts("KNOWN GAP renewal exhaustion at 62 seconds retains the configured address during discovery");

    reply(2, 120); reply(6, 120); state("discovering"); CHECK(!nif.configured);
    acquire(120); tick(60000); reply(6, 120); state("discovering"); CHECK(!nif.configured);
    puts("PASS NAK clears configured in both requesting and renewing states");

    reset(0); acquire(4); tick(4000); state("bound"); CHECK(nif.configured);
    carrier = 0; tick(26000); state("renewing"); CHECK(nif.configured);
    ui_mode("Mode: DHCP (renewing)"); CHECK(!strcmp(labels[42], "Link: down; address configured"));
    CHECK(colors[42] == 0x00E0A060u);
    carrier = 1; tick(1); state("renewing");
    puts("KNOWN GAP 4-second lease remains configured past expiry; T1 has a 30-second floor; carrier changes do not transition DHCP");

    reset(0); acquire(0); tick(1799999); state("bound"); tick(1); state("renewing");
    reset(0); acquire(400000); tick(86399999); state("bound"); tick(1); state("renewing");
    puts("PASS absent/zero lease defaults to 3600 seconds; renewal caps at one day");

    reset(0xfffffff0u); tick(999); CHECK(sends == 1); tick(1); CHECK(sends == 2);
    reset(0xfffffff0u); acquire(120); tick(59999); state("bound"); tick(1); state("renewing");
    puts("PASS retry and renewal deadlines across uint32 millisecond wrap");

    dhcp_stop(); state("idle"); CHECK(!handler); uint32_t old = sends;
    tick(60000); CHECK(sends == old);
    nif.dhcp = 0; ui_mode("Mode: manual (static address)");
    nif.dev = NULL; draw_network(); CHECK(!strcmp(labels[24], "No network card was found."));
    puts("PASS stop cancels timers/UDP listener; Settings shows static mode and no-device state");
    printf("DHCP/Settings: %d checks passed\n", checks);
    return 0;
}
