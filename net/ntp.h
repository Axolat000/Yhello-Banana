#ifndef NTP_H
#define NTP_H

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
#define NTP_IN_KERNEL 1
#include "types.h"
#include "net.h"
#else
#include <stdint.h>
#include <stddef.h>
#ifndef NET_OK
#define NET_OK 0
#define NET_ERR_TIMEOUT (-1)
#define NET_ERR_PROTO (-10)
#define NET_ERR_NOROUTE (-4)
#define NET_ERR_NOMEM (-5)
#define NET_ERR_NOTREADY (-8)
#define NET_ERR_INTR (-11)
#endif
typedef uint32_t ip4_t;
#endif

/*
 * RFC 4330 / RFC 5905 Simple Network Time Protocol (SNTPv4 / NTPv4) client.
 * Contacts an NTP server over UDP port 123, validates stratum, sanity,
 * leap indicators, and calculates time offset and round-trip delay.
 */

#define NTP_PORT 123
#define NTP_PACKET_SIZE 48
#define NTP_EPOCH_DELTA 2208988800ULL /* Seconds between 1900-01-01 and 1970-01-01 */

typedef struct __attribute__((packed)) {
    uint8_t  li_vn_mode;      /* Leap Indicator (2), Version (3), Mode (3) */
    uint8_t  stratum;         /* Stratum level of the local clock */
    uint8_t  poll;            /* Maximum interval between successive messages */
    int8_t   precision;       /* Precision of the local clock */
    uint32_t root_delay;      /* Total round trip delay to the reference clock */
    uint32_t root_dispersion; /* Total dispersion to the reference clock */
    uint32_t ref_id;          /* Reference clock identifier */
    uint32_t ref_ts_sec;      /* Reference timestamp seconds */
    uint32_t ref_ts_frac;     /* Reference timestamp fraction */
    uint32_t orig_ts_sec;     /* Origin timestamp seconds (T1 copied from request) */
    uint32_t orig_ts_frac;    /* Origin timestamp fraction */
    uint32_t recv_ts_sec;     /* Receive timestamp seconds (T2 server receive) */
    uint32_t recv_ts_frac;    /* Receive timestamp fraction */
    uint32_t trans_ts_sec;    /* Transmit timestamp seconds (T3 server transmit) */
    uint32_t trans_ts_frac;   /* Transmit timestamp fraction */
} ntp_packet_t;

typedef struct {
    ip4_t    server_ip;
    uint8_t  stratum;
    uint8_t  leap_indicator;
    int8_t   precision;
    uint32_t roundtrip_ms;    /* measured round-trip delay */
    int64_t  offset_ms;       /* clock offset: positive = local clock is behind server */
    uint32_t server_time_utc; /* calculated Unix UTC seconds corresponding to sync */
} ntp_result_t;

/* Serializes a client request packet. Sets mode=3 (client), vn=4, transmit timestamp from unix_sec/frac */
void ntp_build_request(ntp_packet_t* pkt, uint32_t unix_sec, uint32_t frac);

/* Parses and validates an NTP server response against RFC 4330 / 5905 requirements.
 * Returns NET_OK on success, or a negative error code (e.g. NET_ERR_PROTO). */
int ntp_parse_reply(const ntp_packet_t* pkt, uint32_t len,
                    uint32_t t1_sec, uint32_t t1_ms,
                    uint32_t t4_sec, uint32_t t4_ms,
                    ntp_result_t* res);

/* Queries an NTP server by IPv4 address over UDP port 123.
 * Returns NET_OK on success, or NET_ERR_* on failure. */
int ntp_query_ip(ip4_t server, uint32_t timeout_ms, ntp_result_t* out);

/* Queries an NTP server by hostname or dotted-decimal IP address.
 * Returns NET_OK on success, or NET_ERR_* on failure. */
int ntp_query(const char* host, uint32_t timeout_ms, ntp_result_t* out);

/* Performs network time synchronization against the specified NTP server
 * and applies the adjustment to the system RTC clock.
 * Returns NET_OK on success, or NET_ERR_* on failure. */
int ntp_sync(const char* host, uint32_t timeout_ms, ntp_result_t* out);

#endif
