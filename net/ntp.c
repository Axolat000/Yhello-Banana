/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SNTP over the native UDP stack; no background adjustment or trust claim. */
#include "ntp.h"
#include "timer.h"
#include "rtc.h"
#include "random.h"
#include "kstring.h"
#define EPOCH 2208988800ull
#define PACKET 48
typedef struct {
    ip4_t server;
    uint8_t request[PACKET];
    uint32_t sent_ms, local_utc;
    int done;
    ntp_result_t result;
} query_t;

/* NTP's seconds field wraps in 2036. The RTC supports 2000..2099, a
 * unique interval shorter than one NTP era, so unfold within that interval. */
static int timestamp(const uint8_t* p, uint64_t* ms) {
    uint32_t raw = rd32(p);
    uint64_t sec = (uint64_t)raw + (raw < EPOCH ? (1ull << 32) : 0) - EPOCH;
    if (sec < 946684800ull || sec > 4102444799ull) return -1;
    *ms = sec * 1000 + (((uint64_t)rd32(p + 4) * 1000) >> 32);
    return 0;
}
static void reply(ip4_t src, uint16_t port, const uint8_t* p, uint32_t len, void* ctx) {
    query_t* q = ctx;
    if (q->done || src != q->server || port != 123 || len < PACKET) return;
    unsigned version = (p[0] >> 3) & 7;
    if ((p[0] & 7) != 4 || (version != 3 && version != 4) || (p[0] >> 6) == 3 ||
        !p[1] || p[1] > 15 || memcmp(p + 24, q->request + 40, 8)) return;
    uint64_t t2, t3;
    if (timestamp(p + 32, &t2) || timestamp(p + 40, &t3) || t3 < t2) return;
    uint32_t elapsed = timer_ms() - q->sent_ms;
    uint64_t processing = t3 - t2;
    if (processing > elapsed) return;
    uint32_t delay = elapsed - (uint32_t)processing;
    uint64_t arrival = t3 + delay / 2;
    uint64_t utc = arrival / 1000;
    if (utc > 4102444799ull) return;
    q->result.server = src;
    q->result.utc = (uint32_t)utc;
    q->result.stratum = p[1];
    q->result.roundtrip_ms = delay;
    q->result.offset_ms = (int64_t)((t2 + t3) / 2) -
                         ((int64_t)q->local_utc * 1000 + elapsed / 2);
    q->done = 1;
}
static uint32_t remaining(uint32_t start, uint32_t budget) {
    uint32_t used = timer_ms() - start;
    return used < budget ? budget - used : 0;
}
int ntp_query(const char* host, uint32_t timeout_ms, ntp_result_t* out) {
    if (!out) return NET_ERR_PROTO;
    memset(out, 0, sizeof(*out));
    uint32_t start = timer_ms(), left;
    if (!timeout_ms) return NET_ERR_TIMEOUT;
    if (!net_wait_configured(timeout_ms)) return NET_ERR_NOTREADY;
    if (!(left = remaining(start, timeout_ms))) return NET_ERR_TIMEOUT;
    query_t q;
    memset(&q, 0, sizeof(q));
    if (!host || !*host) host = "pool.ntp.org";
    if (!str_to_ip4(host, &q.server)) {
        int rc = dns_resolve(host, &q.server, left);
        if (rc) return rc;
    }
    if (!q.server) return NET_ERR_NOROUTE;
    if (!remaining(start, timeout_ms)) return NET_ERR_TIMEOUT;
    uint16_t port = udp_ephemeral_port();
    if (!port || udp_bind(port, reply, &q)) return NET_ERR_NOMEM;
    int rc = NET_ERR_TIMEOUT;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!remaining(start, timeout_ms)) break;
        if (net_interrupted()) { rc = NET_ERR_INTR; break; }
        q.request[0] = (4 << 3) | 3;
        q.local_utc = rtc_now_utc();
        wr32(q.request + 40, (uint32_t)(q.local_utc + EPOCH));
        /* Correlate each attempt, including when the RTC is not set. The
         * random fraction is a cookie, not a claim of subsecond RTC accuracy. */
        wr32(q.request + 44, random_u32());
        q.sent_ms = timer_ms();
        rc = udp_send(q.server, port, 123, q.request, sizeof(q.request));
        if (rc) break;
        while (!q.done && timer_ms() - q.sent_ms < 2000) {
            left = remaining(start, timeout_ms);
            if (!left) break;
            if (net_interrupted()) { rc = NET_ERR_INTR; goto done; }
            net_wait(left < 20 ? left : 20);
        }
        if (q.done) { *out = q.result; rc = NET_OK; goto done; }
        rc = NET_ERR_TIMEOUT;
    }
done:
    udp_unbind(port);
    return rc;
}
