/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "net.h"
#include "ipv6.h"
#include "tcp.h"
#include "kheap.h"
#include "kstring.h"
#include "timer.h"
#include "random.h"

/* Stub DNS resolver (RFC 1035): recursive A/AAAA-record queries over UDP to
 * the DHCP-provided server, following CNAMEs inside the answer, with a
 * small TTL-respecting cache. Falls back to TCP when the UDP reply is truncated (TC=1). */

#define DNS_CACHE 16

typedef struct {
    char     name[64];
    ip4_t    ip;
    ip6_t    ip6;
    uint16_t type;
    uint32_t expires_ms;
} dns_cache_t;

static dns_cache_t g_cache[DNS_CACHE];

void dns_flush_cache(void) { memset(g_cache, 0, sizeof(g_cache)); }

/* the one outstanding query (resolution is synchronous per caller, and
 * each waiting task polls its own id) */
typedef struct {
    uint16_t id;
    int      done;
    int      tc;
    int      rcode;
    ip4_t    ip;
    uint32_t ttl;
    ip6_t ip6, server6;
    ip4_t server;
    uint16_t type;
    uint8_t question[256];
    uint32_t qlen;
    int found;
} dns_query_t;

static int cache_get(const char* name, uint16_t type, void* out) {
    for (int i = 0; i < DNS_CACHE; i++) {
        if (g_cache[i].name[0] && g_cache[i].type == type && strcasecmp(g_cache[i].name, name) == 0 &&
            (int32_t)(g_cache[i].expires_ms - timer_ms()) > 0) {
            if (type == 28) *(ip6_t*)out = g_cache[i].ip6;
            else *(ip4_t*)out = g_cache[i].ip;
            return 1;
        }
    }
    return 0;
}

static void cache_put(const char* name, uint16_t type, const void* ip, uint32_t ttl_s) {
    if (strlen(name) >= sizeof(g_cache[0].name)) return;
    if (ttl_s > 3600) ttl_s = 3600;
    if (!ttl_s) return;
    int slot = 0;
    for (int i = 0; i < DNS_CACHE; i++) {
        if (!g_cache[i].name[0] || (g_cache[i].type == type && strcasecmp(g_cache[i].name, name) == 0)) { slot = i; break; }
        if ((int32_t)(g_cache[i].expires_ms - g_cache[slot].expires_ms) < 0) slot = i;
    }
    kstrlcpy(g_cache[slot].name, name, sizeof(g_cache[slot].name));
    g_cache[slot].type = type;
    if (type == 28) g_cache[slot].ip6 = *(const ip6_t*)ip;
    else g_cache[slot].ip = *(const ip4_t*)ip;
    g_cache[slot].expires_ms = timer_ms() + ttl_s * 1000u;
}

/* skips a (possibly compressed) name; returns new offset or 0 on error */
static uint32_t skip_name(const uint8_t* p, uint32_t len, uint32_t off) {
    while (off < len) {
        uint8_t l = p[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) return (off + 2 <= len) ? off + 2 : 0;
        if ((l & 0xC0) || off + 1u + l > len) return 0;
        off += 1u + l;
    }
    return 0;
}

static void parse_reply(dns_query_t* q, const uint8_t* p, uint32_t len) {
    if (len < 12 || rd16(p) != q->id || (p[2] & 0xF8) != 0x80 || rd16(p + 4) != 1) return;
    if (12u + q->qlen + 4u > len || memcmp(p + 12, q->question, q->qlen) ||
        rd16(p + 12 + q->qlen) != q->type || rd16(p + 14 + q->qlen) != 1) return;
    q->rcode = p[3] & 0x0F;
    if (p[2] & 0x02) {
        q->tc = 1;
        q->done = 1;
        return;
    }
    uint16_t qd = rd16(p + 4), an = rd16(p + 6);
    uint32_t off = 12;
    for (uint16_t i = 0; i < qd; i++) {
        off = skip_name(p, len, off);
        if (!off || off + 4 > len) { q->done = 1; return; }
        off += 4;
    }
    /* A recursive server normally includes the final A/AAAA record after
     * a CNAME in the same answer section. Separate CNAME queries are
     * not implemented here. */
    for (uint16_t i = 0; i < an; i++) {
        off = skip_name(p, len, off);
        if (!off || off + 10 > len) break;
        uint16_t type = rd16(p + off), cls = rd16(p + off + 2);
        uint32_t ttl = rd32(p + off + 4);
        uint16_t rdlen = rd16(p + off + 8);
        off += 10;
        if (off + rdlen > len) break;
        if (type == q->type && cls == 1 && rdlen == (q->type == 28 ? 16 : 4)) {
            if (q->type == 28) memcpy(&q->ip6, p + off, 16);
            else q->ip = rd32(p + off);
            q->ttl = ttl; q->found = 1;
            break;
        }
        off += rdlen;
    }
    q->done = 1;
}

static void on_reply(ip4_t src, uint16_t sport, const uint8_t* p, uint32_t len, void* ctx) {
    dns_query_t* q = ctx;
    if (sport == 53 && src == q->server) parse_reply(q, p, len);
}
static void on_reply6(const ip6_t* src, uint16_t sport, const uint8_t* p, uint32_t len, void* ctx) {
    dns_query_t* q = ctx;
    if (sport == 53 && !memcmp(src, &q->server6, 16)) parse_reply(q, p, len);
}

/* One budget covers UDP retries, connect, writes and every fragmented read.
 * Unsigned elapsed time keeps this valid across timer_ms() wrapping. */
static uint32_t dns_remaining(uint32_t start, uint32_t budget) {
    uint32_t elapsed = timer_ms() - start;
    return elapsed < budget ? budget - elapsed : 0;
}

static int tcp_read_all(tcp_conn_t* c, uint8_t* buf, uint32_t len,
                        uint32_t start, uint32_t budget) {
    uint32_t got = 0;
    while (got < len) {
        uint32_t remaining = dns_remaining(start, budget);
        if (!remaining) return NET_ERR_TIMEOUT;
        int r = tcp_recv(c, buf + got, len - got, remaining);
        if (r <= 0) return r == 0 ? NET_ERR_CLOSED : r;
        got += (uint32_t)r;
    }
    return NET_OK;
}

static int resolve_tcp(dns_query_t* q, const uint8_t* pkt, uint32_t qlen,
                       int over6, uint32_t start, uint32_t budget) {
    uint32_t remaining = dns_remaining(start, budget);
    if (!remaining) return NET_ERR_TIMEOUT;
    int err = 0;
    tcp_conn_t* c = over6 ? tcp_connect6(&q->server6, 53, remaining, &err)
                          : tcp_connect(q->server, 53, remaining, &err);
    if (!c) return err ? err : NET_ERR_DNS;
    uint8_t request[302];       /* the UDP query is at most 300 bytes */
    wr16(request, (uint16_t)qlen);
    memcpy(request + 2, pkt, qlen);
    uint32_t sent = 0;
    int rc = NET_ERR_TIMEOUT;
    uint8_t* reply = NULL;
    while (sent < qlen + 2) {
        remaining = dns_remaining(start, budget);
        if (!remaining) goto out;
        int n = tcp_send(c, request + sent, qlen + 2 - sent, remaining);
        if (n <= 0) { rc = n < 0 ? n : NET_ERR_CLOSED; goto out; }
        sent += (uint32_t)n;
    }
    uint8_t size[2];
    rc = tcp_read_all(c, size, 2, start, budget);
    if (rc != NET_OK) goto out;
    uint16_t len = rd16(size);
    /* A bounded heap buffer, never a multi-KiB allocation on the kernel stack. */
    if (len < 12 || len > 4096) { rc = NET_ERR_DNS; goto out; }
    reply = kmalloc(len);
    if (!reply) { rc = NET_ERR_NOMEM; goto out; }
    rc = tcp_read_all(c, reply, len, start, budget);
    if (rc != NET_OK) goto out;
    q->done = q->tc = q->found = 0;
    parse_reply(q, reply, len);
    if (!q->done || q->tc) rc = NET_ERR_DNS;
out:
    if (reply) kfree(reply);
    tcp_close(c);
    return rc;
}

static int encode_name(const char* name, uint8_t* out, uint32_t cap) {
    uint32_t o = 0;
    const char* s = name;
    while (*s) {
        const char* dot = strchr(s, '.');
        uint32_t l = dot ? (uint32_t)(dot - s) : (uint32_t)strlen(s);
        if (l == 0 || l > 63 || o + l + 2 > cap) return -1;
        out[o++] = (uint8_t)l;
        memcpy(out + o, s, l);
        o += l;
        s += l;
        if (*s == '.') s++;
    }
    if (o + 1 > cap) return -1;
    out[o++] = 0;
    return (int)o;
}

static int resolve(const char* name, void* out, uint32_t timeout_ms, uint16_t type) {
    if (!name || !out || !*name) return NET_ERR_DNS;
    if (type == 28) {
        if (!ipv6_enabled()) return NET_ERR_NOTREADY;
        if (ip6_parse(name, out)) return NET_OK;
        if (!strcasecmp(name, "localhost")) { ip6_parse("::1", out); return NET_OK; }
    } else {
        if (str_to_ip4(name, out)) return NET_OK;
        if (!strcasecmp(name, "localhost")) { *(ip4_t*)out = IP4(127, 0, 0, 1); return NET_OK; }
    }
    if (cache_get(name, type, out)) return NET_OK;

    netif_t* nif = net_if();
    if (!nif->dev) return NET_ERR_NODEV;
    ip6_t server6;
    int over6 = type == 28 && ipv6_dns(&server6);
    if (!over6) {
        if (!net_wait_configured(timeout_ms)) return NET_ERR_NOTREADY;
        if (!nif->dns) return NET_ERR_DNS;
    }

    uint8_t pkt[300];
    memset(pkt, 0, 12);
    dns_query_t q;
    memset(&q, 0, sizeof(q));
    q.id = (uint16_t)random_u32();
    q.type = type; q.server = nif->dns;
    if (over6) q.server6 = server6;
    wr16(pkt, q.id);
    wr16(pkt + 2, 0x0100);      /* standard query, recursion desired */
    wr16(pkt + 4, 1);
    int nl = encode_name(name, pkt + 12, sizeof(pkt) - 16);
    if (nl < 0) return NET_ERR_DNS;
    uint32_t qlen = 12u + (uint32_t)nl;
    q.qlen = (uint32_t)nl; memcpy(q.question, pkt + 12, q.qlen);
    wr16(pkt + qlen, type);     /* A or AAAA */
    wr16(pkt + qlen + 2, 1);    /* IN */
    qlen += 4;

    uint16_t port = over6 ? udp6_ephemeral_port() : udp_ephemeral_port();
    if (!port || (over6 ? udp6_bind(port, on_reply6, &q) : udp_bind(port, on_reply, &q)) != 0)
        return NET_ERR_NOMEM;

    int udp_bound = 1;
    int rc = NET_ERR_TIMEOUT;
    uint32_t start = timer_ms();
    for (int attempt = 0; attempt < 3 && !q.done; attempt++) {
        if (type == 28 && !ipv6_enabled()) { rc = NET_ERR_NOTREADY; goto out; }
        int sent_rc = over6 ? udp6_send(&server6, port, 53, pkt, qlen) : udp_send(nif->dns, port, 53, pkt, qlen);
        if (sent_rc != NET_OK) { rc = sent_rc; goto out; }
        uint32_t sent = timer_ms();
        while (!q.done && timer_ms() - sent < 2000u && timer_ms() - start < timeout_ms) {
            if (type == 28 && !ipv6_enabled()) { rc = NET_ERR_NOTREADY; goto out; }
            if (net_interrupted()) { rc = NET_ERR_INTR; goto out; }
            net_wait(20);
        }
        if (timer_ms() - start >= timeout_ms) break;
    }
    if (q.done && q.tc) {
        /* No UDP callback may change the query during blocking TCP operations. */
        if (over6) udp6_unbind(port); else udp_unbind(port);
        udp_bound = 0;
        rc = resolve_tcp(&q, pkt, qlen, over6, start, timeout_ms);
        if (rc != NET_OK) goto out;
    }
    if (q.done) {
        if (q.rcode == 0 && q.found) {
            if (type == 28) *(ip6_t*)out = q.ip6;
            else *(ip4_t*)out = q.ip;
            cache_put(name, type, out, q.ttl);
            rc = NET_OK;
        } else {
            rc = NET_ERR_DNS;
        }
    }
out:
    if (udp_bound) {
        if (over6) udp6_unbind(port); else udp_unbind(port);
    }
    return rc;
}

int dns_resolve(const char* name, ip4_t* out, uint32_t timeout_ms) {
    return resolve(name, out, timeout_ms, 1);
}
int dns_resolve6(const char* name, ip6_t* out, uint32_t timeout_ms) {
    return resolve(name, out, timeout_ms, 28);
}
