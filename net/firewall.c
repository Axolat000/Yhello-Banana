/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "firewall.h"
#include "kstring.h"
#include "timer.h"
#include "serial.h"
#include "settings.h"

/*
 * A host firewall, stateful the simple way:
 *  - TCP: a SYN that opens a connection to us needs its port allowed; the
 *    rest of a connection (ours, or one let in) is not looked at - packets
 *    for connections that do not exist only get a reset anyway.
 *  - UDP: in to a port we sent from in the last few minutes (DNS, DHCP,
 *    UPnP answers...), or to an allowed port.
 *  - ICMP / ICMPv6 echo requests: only with `firewall ping on`; the other
 *    ICMP messages (replies, errors, IPv6 neighbor discovery) pass.
 * Settings: "firewall" on/off, "fw.ping", "fw.tcp" / "fw.udp" port lists.
 */

#define MAX_PORTS 32
#define UDP_OUT   32
#define UDP_OUT_MS (5u * 60u * 1000u)
#define LOG_N     16

static int      g_on, g_ping;
static uint16_t g_tcp[MAX_PORTS], g_udp[MAX_PORTS];
static int      g_ntcp, g_nudp;
static struct { uint16_t port; uint32_t ms; } g_out[UDP_OUT];
static uint32_t g_blocked;
static char     g_log[LOG_N][64];
static int      g_log_at, g_log_n;

static void save_ports(void) {
    char v[MAX_PORTS * 6 + 2];
    for (int p = 0; p < 2; p++) {
        uint16_t* a = p ? g_udp : g_tcp;
        int n = p ? g_nudp : g_ntcp, o = 0;
        v[0] = 0;
        for (int i = 0; i < n; i++) o += ksnprintf(v + o, sizeof(v) - (size_t)o, i ? ",%u" : "%u", a[i]);
        settings_set(p ? "fw.udp" : "fw.tcp", v);
    }
}

static void load_ports(const char* key, uint16_t* a, int* n) {
    char v[MAX_PORTS * 6 + 2];
    *n = 0;
    if (settings_get(key, v, sizeof(v)) <= 0) return;
    const char* s = v;
    while (*s && *n < MAX_PORTS) {
        uint32_t x = 0;
        int d = k_parse_u32(s, &x);
        if (!d) break;
        if (x && x < 65536) a[(*n)++] = (uint16_t)x;
        s += d;
        if (*s == ',') s++;
    }
}

void fw_boot(void) {
    char v[8];
    g_on = settings_get("firewall", v, sizeof(v)) > 0 && !strcmp(v, "on");
    g_ping = settings_get("fw.ping", v, sizeof(v)) > 0 && !strcmp(v, "on");
    load_ports("fw.tcp", g_tcp, &g_ntcp);
    load_ports("fw.udp", g_udp, &g_nudp);
    if (g_on) klog("firewall: on (%d TCP, %d UDP ports allowed in)\n", g_ntcp, g_nudp);
}

int fw_enabled(void) { return g_on; }

void fw_enable(int on) {
    g_on = on ? 1 : 0;
    settings_set("firewall", g_on ? "on" : "off");
    klog("firewall: %s\n", g_on ? "on" : "off");
}

int fw_ping_allowed(void) { return g_ping; }
void fw_set_ping(int allow) { g_ping = allow ? 1 : 0; settings_set("fw.ping", g_ping ? "on" : "off"); }

int fw_allowed(int proto, uint16_t port) {
    uint16_t* a = proto == FW_UDP ? g_udp : g_tcp;
    int n = proto == FW_UDP ? g_nudp : g_ntcp;
    for (int i = 0; i < n; i++) if (a[i] == port) return 1;
    return 0;
}

int fw_allow(int proto, uint16_t port, int allow) {
    uint16_t* a = proto == FW_UDP ? g_udp : g_tcp;
    int* n = proto == FW_UDP ? &g_nudp : &g_ntcp;
    int at = -1;
    for (int i = 0; i < *n; i++) if (a[i] == port) at = i;
    if (allow && at < 0) {
        if (*n >= MAX_PORTS) return -1;
        a[(*n)++] = port;
    } else if (!allow && at >= 0) {
        a[at] = a[--*n];
    }
    save_ports();
    return 0;
}

int fw_ports(int proto, uint16_t* out, int max) {
    int n = proto == FW_UDP ? g_nudp : g_ntcp;
    if (n > max) n = max;
    memcpy(out, proto == FW_UDP ? g_udp : g_tcp, (uint32_t)n * 2);
    return n;
}

uint32_t fw_blocked_count(void) { return g_blocked; }

static void blocked(const char* what, const char* from, uint16_t port) {
    g_blocked++;
    ksnprintf(g_log[g_log_at], sizeof(g_log[0]), port ? "%s from %s to port %u" : "%s from %s", what, from, port);
    g_log_at = (g_log_at + 1) % LOG_N;
    if (g_log_n < LOG_N) g_log_n++;
}

int fw_log(char lines[][64], int max) {
    int n = 0;
    for (int i = 0; i < g_log_n && n < max; i++) {
        int k = (g_log_at - 1 - i + 2 * LOG_N) % LOG_N;
        kstrlcpy(lines[n++], g_log[k], 64);
    }
    return n;
}

int fw_in_tcp(const char* from, uint16_t dport, int syn_only) {
    if (!g_on || !syn_only || fw_allowed(FW_TCP, dport)) return 1;
    blocked("tcp", from, dport);
    return 0;
}

void fw_out_udp(uint16_t sport) {
    uint32_t now = timer_ms();
    int k = 0;
    for (int i = 0; i < UDP_OUT; i++) {
        if (g_out[i].port == sport) { k = i; break; }
        if (g_out[i].ms < g_out[k].ms) k = i;
    }
    g_out[k].port = sport;
    g_out[k].ms = now ? now : 1;
}

int fw_in_udp(const char* from, uint16_t sport, uint16_t dport) {
    (void)sport;
    if (!g_on || fw_allowed(FW_UDP, dport)) return 1;
    uint32_t now = timer_ms();
    for (int i = 0; i < UDP_OUT; i++)
        if (g_out[i].port == dport && g_out[i].ms && now - g_out[i].ms < UDP_OUT_MS) return 1;
    if (dport == 68) return 1;                   /* (DHCP answers: broadcast, to the client port) */
    blocked("udp", from, dport);
    return 0;
}

int fw_in_ping(const char* from) {
    if (!g_on || g_ping) return 1;
    blocked("ping", from, 0);
    return 0;
}
