/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TCP_H
#define TCP_H

#include "net.h"
#include "ipv6.h"

/*
 * TCP (RFC 793 / 1122 / 6298): active and passive open, reliable
 * in-order byte stream with retransmission (RTT-estimated RTO with
 * backoff, fast retransmit on 3 duplicate ACKs), flow control, out-of-
 * order segment reassembly, delayed ACKs and orderly close. Window
 * scaling (RFC 7323): a 512 KiB receive window instead of 64 KiB, so a
 * download is no longer held to 64 KiB per round trip. SACK (RFC 2018):
 * the receiver says which segments it holds beyond a loss, and the
 * sender resends only the holes (NewReno, RFC 6582, without it).
 *
 * The API is blocking and meant for shell-command tasks: every wait
 * polls the stack itself (net_wait()), so it works whether or not netd
 * gets scheduled in between.
 */

typedef struct tcp_conn tcp_conn_t;

typedef enum {
    TCP_CLOSED = 0, TCP_SYN_SENT, TCP_ESTABLISHED, TCP_FIN_WAIT1, TCP_FIN_WAIT2,
    TCP_CLOSE_WAIT, TCP_CLOSING, TCP_LAST_ACK, TCP_TIME_WAIT, TCP_SYN_RCVD
} tcp_state_t;

/* Opens a connection. Returns NULL and sets *err (NET_ERR_*) on failure. */
tcp_conn_t* tcp_connect(ip4_t ip, uint16_t port, uint32_t timeout_ms, int* err);
tcp_conn_t* tcp_connect6(const ip6_t* ip, uint16_t port, uint32_t timeout_ms, int* err);
void tcp_rx6(const ip6_t* src, const ip6_t* dst, const uint8_t* seg, uint32_t len);
void tcp6_disable(void); /* invalidate IPv6 sessions, preserving IPv4 */

/* Queues all `len` bytes (blocking while the send buffer is full).
 * Returns len, or a NET_ERR_* code. */
int tcp_send(tcp_conn_t* c, const void* data, uint32_t len, uint32_t timeout_ms);

/* Receives up to `max` bytes. Returns >0 bytes, 0 at end of stream
 * (peer closed), or a NET_ERR_* code (timeout, reset, interrupted). */
int tcp_recv(tcp_conn_t* c, void* buf, uint32_t max, uint32_t timeout_ms);

/* Graceful close (FIN after queued data). The connection object belongs
 * to the stack afterwards and is freed once the close completes. */
void tcp_close(tcp_conn_t* c);

/* Immediate close with RST. */
void tcp_abort(tcp_conn_t* c);
void tcp_network_changed(void); /* fail external connections; retain listeners/loopback */

/* ── server side (passive open) ─────────────────────────────────
 * tcp_listen() starts answering SYNs on a local port; connections that
 * complete the handshake queue up (a few per listener) until
 * tcp_accept() hands them out. Accepted connections are used exactly
 * like tcp_connect() ones. */
typedef struct tcp_listener tcp_listener_t;
tcp_listener_t* tcp_listen(uint16_t port, int* err);
tcp_listener_t* tcp_listen6(uint16_t port, int* err);
/* waits up to timeout_ms (0: just checks) for a connection; NULL if none */
tcp_conn_t* tcp_accept(tcp_listener_t* l, uint32_t timeout_ms);
void tcp_unlisten(tcp_listener_t* l);   /* also resets unaccepted connections */
void tcp_peer(const tcp_conn_t* c, ip4_t* ip, uint16_t* port);
int tcp_peer6(const tcp_conn_t* c, ip6_t* ip, uint16_t* port); /* 0 for IPv4 */
/* bytes waiting to be read; -1 once nothing more can arrive (FIN/RST) */
int  tcp_readable(const tcp_conn_t* c);
/* bytes that can be queued with tcp_send() without blocking */
uint32_t tcp_send_space(const tcp_conn_t* c);

tcp_state_t tcp_state(const tcp_conn_t* c);
const char* tcp_state_str(tcp_state_t s);

void tcp_dump(void);     /* netstat */

/* for sockets (kernel/usock.c): received bytes copied without taking them;
 * connections waiting for tcp_accept(); the local port */
int      tcp_peek(const tcp_conn_t* c, void* buf, uint32_t max);
int      tcp_pending(const tcp_listener_t* l);
uint16_t tcp_local_port(const tcp_conn_t* c);

/* what a connection agreed on (selftest net, netstat) */
typedef struct {
    int      wscale, sack;              /* both sides use window scaling / SACK */
    uint8_t  snd_ws, rcv_ws;            /* the shifts: the peer's, ours */
    uint32_t rcv_window;                /* the window we offer now (bytes) */
    uint32_t snd_window;                /* the peer's (bytes) */
} tcp_info_t;
void tcp_info(const tcp_conn_t* c, tcp_info_t* i);

typedef struct {
    uint32_t fast_rexmit;               /* losses found by 3 duplicate ACKs */
    uint32_t sack_rexmit;               /* holes resent by what SACK said */
    uint32_t rto_rexmit;                /* retransmission timeouts */
    uint32_t sack_acks;                 /* ACKs we sent with SACK blocks */
    uint32_t test_dropped;              /* segments tcp_test_loss dropped */
} tcp_stats_t;
void tcp_stats(tcp_stats_t* s);         /* since boot */
/* selftest net: every nth data segment to a loopback (127.x) peer is
 * dropped instead of sent (0: none) */
void tcp_test_loss(uint32_t every);

#endif
