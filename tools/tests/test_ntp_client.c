#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ntp.h"

/*
 * Standalone host unit test for RFC 4330 / 5905 SNTP client implementation (net/ntp.c).
 * - Tests packet serialization (ntp_build_request)
 * - Tests packet validation and parsing (ntp_parse_reply) under normal and malformed inputs
 * - Runs a mock UDP NTP server on 127.0.0.1 and tests live packet exchange,
 *   stratum validation, delay and offset calculation.
 */

static void test_packet_format_and_parsing(void) {
    printf("1. Testing NTP packet serialization and parse validation...\n");

    ntp_packet_t req;
    uint32_t t1_sec = 1776510000u;
    uint32_t t1_frac = 0x80000000u; /* 500 ms */
    ntp_build_request(&req, t1_sec, t1_frac);

    assert(sizeof(ntp_packet_t) == 48);
    assert((req.li_vn_mode & 0x07) == 3); /* Client mode */
    assert(((req.li_vn_mode >> 3) & 0x07) == 4); /* NTPv4 */
    assert(((req.li_vn_mode >> 6) & 0x03) == 0); /* LI = 0 */

    uint32_t wire_sec = ntohl(req.trans_ts_sec);
    assert(wire_sec == (uint32_t)(t1_sec + NTP_EPOCH_DELTA));
    assert(ntohl(req.trans_ts_frac) == t1_frac);

    /* Test valid server reply */
    ntp_packet_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.li_vn_mode = (0 << 6) | (4 << 3) | 4; /* LI=0, VN=4, Mode=4 (server) */
    reply.stratum = 2;
    reply.precision = -20;

    /* T2: server receive at 1776510000s + 600ms */
    reply.recv_ts_sec = htonl((uint32_t)(1776510000u + NTP_EPOCH_DELTA));
    reply.recv_ts_frac = htonl((uint32_t)(((uint64_t)600 << 32) / 1000ULL));

    /* T3: server transmit at 1776510000s + 610ms */
    reply.trans_ts_sec = htonl((uint32_t)(1776510000u + NTP_EPOCH_DELTA));
    reply.trans_ts_frac = htonl((uint32_t)(((uint64_t)610 << 32) / 1000ULL));

    /* T1: client send at 1776510000s + 500ms */
    /* T4: client receive at 1776510000s + 750ms */
    ntp_result_t res;
    memset(&res, 0, sizeof(res));
    int rc = ntp_parse_reply(&reply, sizeof(reply),
                             1776510000u, 500,
                             1776510000u, 750,
                             &res);
    assert(rc == NET_OK);
    assert(res.stratum == 2);
    assert(res.leap_indicator == 0);
    assert(res.precision == -20);
    /*
     * Roundtrip delay = (T4 - T1) - (T3 - T2) = (250 ms) - (10 ms) = 240 ms
     * Offset = ((T2 - T1) + (T3 - T4)) / 2 = ((100 ms) + (-140 ms)) / 2 = -20 ms
     */
    assert(res.roundtrip_ms == 240);
    assert(res.offset_ms == -20);
    assert(res.server_time_utc == 1776510000u);

    /* Test malformed / invalid replies */
    /* 1. Too short */
    assert(ntp_parse_reply(&reply, 40, 1776510000u, 500, 1776510000u, 750, &res) == NET_ERR_PROTO);

    /* 2. Mode not server */
    reply.li_vn_mode = (0 << 6) | (4 << 3) | 3;
    assert(ntp_parse_reply(&reply, sizeof(reply), 1776510000u, 500, 1776510000u, 750, &res) == NET_ERR_PROTO);

    /* 3. LI = 3 (unsynchronized) */
    reply.li_vn_mode = (3 << 6) | (4 << 3) | 4;
    assert(ntp_parse_reply(&reply, sizeof(reply), 1776510000u, 500, 1776510000u, 750, &res) == NET_ERR_PROTO);

    /* 4. Stratum 0 (Kiss-o'-Death) */
    reply.li_vn_mode = (0 << 6) | (4 << 3) | 4;
    reply.stratum = 0;
    assert(ntp_parse_reply(&reply, sizeof(reply), 1776510000u, 500, 1776510000u, 750, &res) == NET_ERR_PROTO);

    /* 5. Stratum 16 (unsynchronized) */
    reply.stratum = 16;
    assert(ntp_parse_reply(&reply, sizeof(reply), 1776510000u, 500, 1776510000u, 750, &res) == NET_ERR_PROTO);

    /* 6. Pre-1970 timestamp */
    reply.stratum = 1;
    reply.trans_ts_sec = htonl(1000u);
    assert(ntp_parse_reply(&reply, sizeof(reply), 1776510000u, 500, 1776510000u, 750, &res) == NET_ERR_PROTO);

    printf("PASS: Packet serialization, offset calculation, and validation logic verified.\n");
}

/* ── Mock NTP Server on Local UDP Socket ─────────────────────────────── */

typedef struct {
    int port;
    int stop;
    uint32_t srv_utc_sec;
} mock_server_args_t;

static void* mock_ntp_server_thread(void* arg) {
    mock_server_args_t* args = (mock_server_args_t*)arg;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    assert(s >= 0);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(args->port);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    assert(bind(s, (struct sockaddr*)&sin, sizeof(sin)) == 0);

    struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (!args->stop) {
        ntp_packet_t req;
        struct sockaddr_in client_sin;
        socklen_t slen = sizeof(client_sin);
        ssize_t n = recvfrom(s, &req, sizeof(req), 0, (struct sockaddr*)&client_sin, &slen);
        if (n >= (ssize_t)sizeof(req)) {
            /* Verify client request format */
            uint8_t mode = req.li_vn_mode & 0x07;
            if (mode == 3) {
                ntp_packet_t reply;
                memset(&reply, 0, sizeof(reply));
                reply.li_vn_mode = (0 << 6) | (4 << 3) | 4; /* LI=0, VN=4, Mode=4 */
                reply.stratum = 1; /* Primary reference */
                reply.poll = 4;
                reply.precision = -20;
                reply.root_delay = 0;
                reply.root_dispersion = htonl(100);
                reply.ref_id = *(uint32_t*)"GPS ";

                /* Copy client transmit timestamp to orig_ts */
                reply.orig_ts_sec = req.trans_ts_sec;
                reply.orig_ts_frac = req.trans_ts_frac;

                /* Set server receive & transmit timestamps */
                uint32_t sec = args->srv_utc_sec;
                reply.ref_ts_sec = htonl((uint32_t)(sec - 10 + NTP_EPOCH_DELTA));
                reply.recv_ts_sec = htonl((uint32_t)(sec + NTP_EPOCH_DELTA));
                reply.recv_ts_frac = htonl(0x10000000u);
                reply.trans_ts_sec = htonl((uint32_t)(sec + NTP_EPOCH_DELTA));
                reply.trans_ts_frac = htonl(0x15000000u);

                sendto(s, &reply, sizeof(reply), 0, (struct sockaddr*)&client_sin, slen);
            }
        }
    }
    close(s);
    return NULL;
}

static void test_mock_ntp_server_exchange(void) {
    printf("2. Testing live UDP query/response exchange with mock NTP server...\n");

    mock_server_args_t srv_args;
    srv_args.port = 12123;
    srv_args.stop = 0;
    srv_args.srv_utc_sec = 1776512345u;

    pthread_t th;
    assert(pthread_create(&th, NULL, mock_ntp_server_thread, &srv_args) == 0);
    usleep(20000); /* 20ms warm-up */

    int client_sock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(client_sock >= 0);

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ntp_packet_t req;
    uint32_t client_t1_sec = 1776512340u;
    uint32_t client_t1_ms = 100;
    uint32_t client_t1_frac = (uint32_t)(((uint64_t)client_t1_ms << 32) / 1000ULL);
    ntp_build_request(&req, client_t1_sec, client_t1_frac);

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(srv_args.port);
    dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    ssize_t sent = sendto(client_sock, &req, sizeof(req), 0, (struct sockaddr*)&dest, sizeof(dest));
    assert(sent == (ssize_t)sizeof(req));

    ntp_packet_t reply;
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    ssize_t rcvd = recvfrom(client_sock, &reply, sizeof(reply), 0, (struct sockaddr*)&from, &flen);
    assert(rcvd == (ssize_t)sizeof(reply));

    uint32_t client_t4_sec = 1776512340u;
    uint32_t client_t4_ms = 120;
    ntp_result_t res;
    memset(&res, 0, sizeof(res));

    int rc = ntp_parse_reply(&reply, (uint32_t)rcvd,
                             client_t1_sec, client_t1_ms,
                             client_t4_sec, client_t4_ms,
                             &res);
    assert(rc == NET_OK);
    assert(res.stratum == 1);
    assert(res.leap_indicator == 0);
    assert(res.server_time_utc >= 1776512344u && res.server_time_utc <= 1776512346u);
    /* Local clock was 1776512340, server was 1776512345: positive offset ~5000 ms */
    assert(res.offset_ms >= 4900 && res.offset_ms <= 5100);

    close(client_sock);
    srv_args.stop = 1;
    pthread_join(th, NULL);

    printf("PASS: Mock NTP server exchange verified with positive offset and stratum 1.\n");
}

int main(void) {
    printf("=== RFC 4330 / 5905 SNTP Client Unit Test ===\n");
    test_packet_format_and_parsing();
    test_mock_ntp_server_exchange();
    printf("All SNTP client tests passed successfully.\n");
    return 0;
}
