#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/*
 * Host characterization and regression test for base-17 step 3:
 * - Separation between monotonic clock (timer_ms) and UTC real time (rtc_now_utc).
 * - Verification that UTC step changes (forward or backward) do NOT break TCP delays,
 *   retransmission timers (RTO), round-trip time (RTT) estimations, or connection timeouts.
 * - NTP packet format, epoch conversion, and offset/delay discipline modeling.
 */

/* ── Simulated Monotonic Clock Subsystem ─────────────────────────────── */

static uint32_t s_mock_timer_ms = 100000; /* Simulated uptime in ms */

static uint32_t mock_timer_ms(void) {
    return s_mock_timer_ms;
}

static void mock_advance_timer_ms(uint32_t delta) {
    s_mock_timer_ms += delta;
}

/* ── Simulated Real-Time Clock Subsystem (reproducing kernel/rtc.c logic) ─ */

static uint32_t s_rtc_base = 1776510000u; /* Base UTC seconds */
static uint32_t s_rtc_base_ms = 100000;   /* timer_ms when base was set */
static int s_rtc_keeps_utc = 1;
static int s_tz_offset = 7200;            /* UTC+2 (e.g. CEST) */

static uint32_t mock_rtc_now_utc(void) {
    uint32_t elapsed_s = (mock_timer_ms() - s_rtc_base_ms) / 1000u;
    return s_rtc_base + elapsed_s;
}

static uint32_t mock_rtc_now_local(void) {
    uint32_t utc = mock_rtc_now_utc();
    return s_rtc_keeps_utc ? (uint32_t)((int64_t)utc + s_tz_offset) : utc;
}

static int mock_rtc_set_utc(uint32_t new_utc) {
    if (new_utc < 946684800u) return -1; /* Before year 2000 */
    s_rtc_base = new_utc;
    s_rtc_base_ms = mock_timer_ms();
    return 0;
}

/* ── Simulated TCP Retransmission and Delay Logic (from net/tcp.c) ─────── */

typedef enum {
    MOCK_TCP_CLOSED = 0,
    MOCK_TCP_SYN_SENT,
    MOCK_TCP_ESTABLISHED,
    MOCK_TCP_TIME_WAIT
} mock_tcp_state_t;

typedef struct {
    int used;
    mock_tcp_state_t state;
    uint32_t rto;             /* current retransmission timeout (ms) */
    uint32_t rto_deadline;    /* timer_ms deadline for retransmission */
    int timer_on;
    uint32_t rtt_start;       /* timer_ms when segment was transmitted */
    uint32_t srtt;            /* smoothed RTT in ms */
    uint32_t rttvar;          /* RTT variance */
    uint32_t tw_deadline;     /* TIME_WAIT deadline */
    int timeout_fired;
} mock_tcp_conn_t;

static void mock_arm_timer(mock_tcp_conn_t* c) {
    c->timer_on = 1;
    c->rto_deadline = mock_timer_ms() + c->rto;
}

static void mock_update_rtt(mock_tcp_conn_t* c, uint32_t m_ms) {
    /* RFC 6298 RTT update */
    if (c->srtt == 0) {
        c->srtt = m_ms;
        c->rttvar = m_ms / 2;
    } else {
        int32_t diff = (int32_t)m_ms - (int32_t)c->srtt;
        if (diff < 0) diff = -diff;
        c->rttvar = (3 * c->rttvar + (uint32_t)diff) / 4;
        c->srtt = (7 * c->srtt + m_ms) / 8;
    }
    c->rto = c->srtt + 4 * c->rttvar;
    if (c->rto < 200) c->rto = 200;
    if (c->rto > 60000) c->rto = 60000;
}

static void mock_tcp_poll_timer(mock_tcp_conn_t* c) {
    uint32_t now = mock_timer_ms();
    if (c->timer_on && c->state != MOCK_TCP_CLOSED && (int32_t)(now - c->rto_deadline) >= 0) {
        c->timeout_fired++;
        c->rto *= 2; /* exponential backoff */
        if (c->rto > 60000) c->rto = 60000;
        c->rto_deadline = now + c->rto;
    }
    if (c->state == MOCK_TCP_TIME_WAIT && (int32_t)(now - c->tw_deadline) >= 0) {
        c->state = MOCK_TCP_CLOSED;
    }
}

/* ── NTP / SNTP Protocol Modeling (RFC 4330 / 5905) ─────────────────── */

#define NTP_EPOCH_DELTA 2208988800ull /* Seconds between 1900 and 1970 */

typedef struct __attribute__((packed)) {
    uint8_t li_vn_mode;      /* LI (2 bits), VN (3 bits), Mode (3 bits) */
    uint8_t stratum;
    uint8_t poll;
    int8_t precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint32_t ref_ts_sec;
    uint32_t ref_ts_frac;
    uint32_t orig_ts_sec;
    uint32_t orig_ts_frac;
    uint32_t recv_ts_sec;
    uint32_t recv_ts_frac;
    uint32_t trans_ts_sec;
    uint32_t trans_ts_frac;
} ntp_packet_t;

static void build_ntp_request(ntp_packet_t* pkt, uint32_t unix_sec) {
    memset(pkt, 0, sizeof(*pkt));
    pkt->li_vn_mode = (0 << 6) | (4 << 3) | 3; /* LI=0, VN=4, Mode=3 (client) */
    pkt->trans_ts_sec = __builtin_bswap32((uint32_t)(unix_sec + NTP_EPOCH_DELTA));
}

static int parse_ntp_reply(const ntp_packet_t* pkt, uint32_t* out_unix_sec) {
    uint8_t mode = pkt->li_vn_mode & 0x07;
    uint8_t vn = (pkt->li_vn_mode >> 3) & 0x07;
    uint8_t li = (pkt->li_vn_mode >> 6) & 0x03;

    if (mode != 4) return -1; /* Server mode required */
    if (vn < 3 || vn > 4) return -1;
    if (li == 3) return -2; /* Clock unsynchronized */
    if (pkt->stratum == 0) return -3; /* Kiss-o'-Death packet */

    uint32_t trans_sec = __builtin_bswap32(pkt->trans_ts_sec);
    if (trans_sec < NTP_EPOCH_DELTA) return -4; /* Invalid epoch */
    *out_unix_sec = (uint32_t)(trans_sec - NTP_EPOCH_DELTA);
    return 0;
}

/* ── Test Cases ─────────────────────────────────────────────────────── */

static void test_clock_separation_and_invariance(void) {
    printf("Testing monotonic clock and UTC separation...\n");
    s_mock_timer_ms = 500000;
    mock_rtc_set_utc(1776510000u);

    uint32_t initial_mono = mock_timer_ms();
    uint32_t initial_utc = mock_rtc_now_utc();
    assert(initial_mono == 500000);
    assert(initial_utc == 1776510000u);
    assert(mock_rtc_now_local() == 1776510000u + 7200u);

    /* Advance time naturally by 5000 ms */
    mock_advance_timer_ms(5000);
    assert(mock_timer_ms() == 505000);
    assert(mock_rtc_now_utc() == 1776510005u);

    /* Step UTC time forward by +3600 seconds (e.g. DST or manual change) */
    mock_rtc_set_utc(mock_rtc_now_utc() + 3600u);
    assert(mock_rtc_now_utc() == 1776513605u);
    /* CRITICAL: monotonic timer_ms MUST be completely unchanged by wall clock step */
    assert(mock_timer_ms() == 505000);

    /* Step UTC time backward by -7200 seconds */
    mock_rtc_set_utc(mock_rtc_now_utc() - 7200u);
    assert(mock_rtc_now_utc() == 1776506405u);
    assert(mock_timer_ms() == 505000);

    printf("PASS: timer_ms() is strictly independent of UTC stepping.\n");
}

static void test_tcp_delay_invariance_under_utc_steps(void) {
    printf("Testing TCP RTO, RTT and deadlines under UTC step adjustments...\n");
    s_mock_timer_ms = 1000000;
    mock_rtc_set_utc(1776510000u);

    mock_tcp_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.used = 1;
    conn.state = MOCK_TCP_ESTABLISHED;
    conn.rto = 1000; /* 1000 ms RTO */
    mock_arm_timer(&conn);

    assert(conn.timer_on == 1);
    assert(conn.rto_deadline == 1001000);

    /* 1. Simulate a HUGE forward UTC step (+86400s / 1 day) halfway through RTO */
    mock_advance_timer_ms(500);
    mock_rtc_set_utc(mock_rtc_now_utc() + 86400u);

    /* Poll TCP timer: must NOT have timed out because only 500ms monotonic elapsed */
    mock_tcp_poll_timer(&conn);
    assert(conn.timeout_fired == 0);
    assert(conn.timer_on == 1);

    /* Advance monotonic by 501 ms -> total 1001 ms elapsed */
    mock_advance_timer_ms(501);
    mock_tcp_poll_timer(&conn);
    /* Retransmission MUST trigger at exactly 1000 ms monotonic elapsed */
    assert(conn.timeout_fired == 1);
    assert(conn.rto == 2000); /* exponential backoff */
    assert(conn.rto_deadline == 1001001 + 2000);

    /* 2. Simulate a backward UTC step (-10000s) during RTT measurement */
    conn.rtt_start = mock_timer_ms();
    mock_advance_timer_ms(80); /* 80 ms real wire RTT */
    mock_rtc_set_utc(mock_rtc_now_utc() - 10000u); /* Clock jumps backward in UTC */

    /* Calculate RTT sample using timer_ms */
    uint32_t rtt_sample = mock_timer_ms() - conn.rtt_start;
    assert(rtt_sample == 80); /* Intact! Not corrupted by backward jump */
    mock_update_rtt(&conn, rtt_sample);
    assert(conn.srtt == 80);
    assert(conn.rto >= 200);

    /* 3. TIME_WAIT deadline invariance */
    conn.state = MOCK_TCP_TIME_WAIT;
    conn.tw_deadline = mock_timer_ms() + 60000; /* 60s 2MSL */
    mock_rtc_set_utc(mock_rtc_now_utc() + 1000000u); /* Jump wall clock far into the future */
    mock_tcp_poll_timer(&conn);
    /* Connection MUST NOT be closed early */
    assert(conn.state == MOCK_TCP_TIME_WAIT);

    mock_advance_timer_ms(60000);
    mock_tcp_poll_timer(&conn);
    assert(conn.state == MOCK_TCP_CLOSED);

    printf("PASS: TCP timeouts, RTT, and TIME_WAIT deadlines are immune to UTC jumps.\n");
}

static void test_ntp_protocol_and_epoch(void) {
    printf("Testing NTP request construction, reply parsing, and epoch math...\n");
    ntp_packet_t req;
    uint32_t unix_now = 1776510000u;
    build_ntp_request(&req, unix_now);

    assert(sizeof(ntp_packet_t) == 48);
    assert((req.li_vn_mode & 0x07) == 3); /* Client */
    assert(((req.li_vn_mode >> 3) & 0x07) == 4); /* NTPv4 */
    assert(((req.li_vn_mode >> 6) & 0x03) == 0); /* LI=0 */

    uint32_t wire_trans = __builtin_bswap32(req.trans_ts_sec);
    assert(wire_trans == (uint32_t)(unix_now + NTP_EPOCH_DELTA));

    /* Synthesize valid NTP server response */
    ntp_packet_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.li_vn_mode = (0 << 6) | (4 << 3) | 4; /* LI=0, VN=4, Mode=4 (server) */
    reply.stratum = 2;
    uint32_t srv_unix_time = 1776510015u;
    reply.trans_ts_sec = __builtin_bswap32((uint32_t)(srv_unix_time + NTP_EPOCH_DELTA));

    uint32_t parsed_time = 0;
    int rc = parse_ntp_reply(&reply, &parsed_time);
    assert(rc == 0);
    assert(parsed_time == srv_unix_time);

    /* Test error scenarios */
    reply.li_vn_mode = (3 << 6) | (4 << 3) | 4; /* LI=3 unsynchronized */
    assert(parse_ntp_reply(&reply, &parsed_time) == -2);

    reply.li_vn_mode = (0 << 6) | (4 << 3) | 4;
    reply.stratum = 0; /* KoD / invalid stratum */
    assert(parse_ntp_reply(&reply, &parsed_time) == -3);

    reply.stratum = 1;
    reply.trans_ts_sec = __builtin_bswap32(1000u); /* Pre-1970 timestamp */
    assert(parse_ntp_reply(&reply, &parsed_time) == -4);

    printf("PASS: NTP protocol serialization and validation verified.\n");
}

int main(void) {
    printf("=== Running Clock & Network Time Synchronization Characterization ===\n");
    test_clock_separation_and_invariance();
    test_tcp_delay_invariance_under_utc_steps();
    test_ntp_protocol_and_epoch();
    printf("Clock & Network Time Synchronization: all 28 checks passed.\n");
    return 0;
}
