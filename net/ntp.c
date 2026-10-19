#include "ntp.h"

#ifdef NTP_IN_KERNEL
#include "kstring.h"
#include "timer.h"
#include "rtc.h"
#include "random.h"
#else
#include <string.h>
#include <arpa/inet.h>
#endif

/*
 * RFC 4330 / RFC 5905 Simple Network Time Protocol (SNTPv4 / NTPv4) client.
 *
 * Implements query via UDP port 123 to pool.ntp.org or custom NTP servers.
 * Converts between Unix epoch (1970) and NTP epoch (1900).
 * Calculates round-trip delay and clock offset, verifying server stratum and sanity.
 */

void ntp_build_request(ntp_packet_t* pkt, uint32_t unix_sec, uint32_t frac) {
    if (!pkt) return;
    memset(pkt, 0, sizeof(*pkt));
    /* LI = 0 (no warning), VN = 4 (IPv4 NTPv4), Mode = 3 (Client) */
    pkt->li_vn_mode = (0 << 6) | (4 << 3) | 3;
    pkt->trans_ts_sec = htonl((uint32_t)(unix_sec + NTP_EPOCH_DELTA));
    pkt->trans_ts_frac = htonl(frac);
}

int ntp_parse_reply(const ntp_packet_t* pkt, uint32_t len,
                    uint32_t t1_sec, uint32_t t1_ms,
                    uint32_t t4_sec, uint32_t t4_ms,
                    ntp_result_t* res) {
    if (!pkt || len < NTP_PACKET_SIZE) return NET_ERR_PROTO;

    uint8_t mode = pkt->li_vn_mode & 0x07;
    uint8_t vn = (pkt->li_vn_mode >> 3) & 0x07;
    uint8_t li = (pkt->li_vn_mode >> 6) & 0x03;

    /* Mode 4 is server response; mode 5 is broadcast (accept server mode) */
    if (mode != 4) return NET_ERR_PROTO;
    if (vn < 3 || vn > 4) return NET_ERR_PROTO;
    if (li == 3) return NET_ERR_PROTO; /* Clock not synchronized */
    if (pkt->stratum == 0) return NET_ERR_PROTO; /* Kiss-o'-Death or unspecified */
    if (pkt->stratum > 15) return NET_ERR_PROTO; /* Reserved / unsynchronized */

    uint32_t t2_sec_raw = ntohl(pkt->recv_ts_sec);
    uint32_t t2_frac = ntohl(pkt->recv_ts_frac);
    uint32_t t3_sec_raw = ntohl(pkt->trans_ts_sec);
    uint32_t t3_frac = ntohl(pkt->trans_ts_frac);

    /* Validate epoch bounds */
    if (t3_sec_raw < NTP_EPOCH_DELTA) return NET_ERR_PROTO;

    uint32_t t2_sec = (t2_sec_raw >= NTP_EPOCH_DELTA) ? (uint32_t)(t2_sec_raw - NTP_EPOCH_DELTA) : 0;
    uint32_t t3_sec = (uint32_t)(t3_sec_raw - NTP_EPOCH_DELTA);

    /* Convert fractions (2^32 = 1000 ms) with rounding */
    uint32_t t2_ms = (uint32_t)((((uint64_t)t2_frac * 1000ULL) + 0x80000000ULL) >> 32);
    uint32_t t3_ms = (uint32_t)((((uint64_t)t3_frac * 1000ULL) + 0x80000000ULL) >> 32);

    int64_t t1_total_ms = (int64_t)t1_sec * 1000LL + (int64_t)t1_ms;
    int64_t t2_total_ms = (int64_t)t2_sec * 1000LL + (int64_t)t2_ms;
    int64_t t3_total_ms = (int64_t)t3_sec * 1000LL + (int64_t)t3_ms;
    int64_t t4_total_ms = (int64_t)t4_sec * 1000LL + (int64_t)t4_ms;

    /*
     * RFC 5905:
     * Roundtrip delay d = (T4 - T1) - (T3 - T2)
     * Clock offset theta = ((T2 - T1) + (T3 - T4)) / 2
     */
    int64_t delay = (t4_total_ms - t1_total_ms) - (t3_total_ms - t2_total_ms);
    if (delay < 0) delay = 0;

    int64_t offset = ((t2_total_ms - t1_total_ms) + (t3_total_ms - t4_total_ms)) / 2LL;

    if (res) {
        res->stratum = pkt->stratum;
        res->leap_indicator = li;
        res->precision = pkt->precision;
        res->roundtrip_ms = (uint32_t)delay;
        res->offset_ms = offset;
        /* Calculate server time corresponding to client receive time T4 */
        int64_t corrected_now_ms = t4_total_ms + offset;
        res->server_time_utc = (uint32_t)(corrected_now_ms / 1000LL);
    }

    return NET_OK;
}

#ifdef NTP_IN_KERNEL

typedef struct {
    int          done;
    ip4_t        src_ip;
    uint32_t     recv_mono_ms;
    uint32_t     recv_utc_sec;
    uint32_t     recv_utc_ms;
    uint32_t     len;
    ntp_packet_t pkt;
} ntp_query_ctx_t;

static void on_ntp_reply(ip4_t src, uint16_t sport, const uint8_t* data, uint32_t len, void* ctx) {
    (void)sport;
    ntp_query_ctx_t* q = (ntp_query_ctx_t*)ctx;
    if (!q || q->done) return;
    if (len < NTP_PACKET_SIZE) return;

    q->recv_mono_ms = timer_ms();
    q->recv_utc_sec = rtc_now_utc();
    q->recv_utc_ms = q->recv_mono_ms % 1000u;
    q->src_ip = src;
    q->len = len > sizeof(q->pkt) ? sizeof(q->pkt) : len;
    memcpy(&q->pkt, data, q->len);
    q->done = 1;
}

int ntp_query_ip(ip4_t server, uint32_t timeout_ms, ntp_result_t* out) {
    if (!server) return NET_ERR_NOROUTE;
    if (!net_wait_configured(timeout_ms)) return NET_ERR_NOTREADY;

    uint16_t port = udp_ephemeral_port();
    if (!port) return NET_ERR_NOMEM;

    ntp_query_ctx_t q;
    memset(&q, 0, sizeof(q));

    if (udp_bind(port, on_ntp_reply, &q) != 0) {
        return NET_ERR_NOMEM;
    }

    if (timeout_ms < 1000) timeout_ms = 1000;

    int rc = NET_ERR_TIMEOUT;
    uint32_t start_ms = timer_ms();

    for (int attempt = 0; attempt < 3 && !q.done; attempt++) {
        uint32_t t1_mono_ms = timer_ms();
        uint32_t t1_sec = rtc_now_utc();
        uint32_t t1_ms = t1_mono_ms % 1000u;
        uint32_t t1_frac = (uint32_t)(((uint64_t)t1_ms << 32) / 1000ULL);

        ntp_packet_t req;
        ntp_build_request(&req, t1_sec, t1_frac);

        int sent_rc = udp_send(server, port, NTP_PORT, &req, sizeof(req));
        if (sent_rc != NET_OK) {
            rc = sent_rc;
            goto cleanup;
        }

        uint32_t sent_ms = timer_ms();
        while (!q.done && (timer_ms() - sent_ms < 2000u) && (timer_ms() - start_ms < timeout_ms)) {
            if (net_interrupted()) {
                rc = NET_ERR_INTR;
                goto cleanup;
            }
            net_wait(20);
        }

        if (q.done) {
            /* Validate and parse packet */
            rc = ntp_parse_reply(&q.pkt, q.len, t1_sec, t1_ms, q.recv_utc_sec, q.recv_utc_ms, out);
            if (rc == NET_OK && out) {
                out->server_ip = server;
            }
            goto cleanup;
        }

        if (timer_ms() - start_ms >= timeout_ms) break;
    }

cleanup:
    udp_unbind(port);
    return rc;
}

int ntp_query(const char* host, uint32_t timeout_ms, ntp_result_t* out) {
    if (!host || !*host) host = "pool.ntp.org";

    ip4_t server_ip = 0;
    if (!str_to_ip4(host, &server_ip)) {
        int dns_rc = dns_resolve(host, &server_ip, timeout_ms / 2 ? timeout_ms / 2 : 2000);
        if (dns_rc != NET_OK) return dns_rc;
    }

    return ntp_query_ip(server_ip, timeout_ms, out);
}

int ntp_sync(const char* host, uint32_t timeout_ms, ntp_result_t* out) {
    ntp_result_t res;
    memset(&res, 0, sizeof(res));

    int rc = ntp_query(host, timeout_ms, &res);
    if (rc != NET_OK) return rc;

    /* Update RTC time. Slewing discipline will be integrated in step 3. */
    if (res.server_time_utc >= 946684800u) {
        rtc_set_utc(res.server_time_utc);
    }

    if (out) *out = res;
    return NET_OK;
}
#endif
