/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TLS12_H
#define TLS12_H

#include "tcp.h"
#include "rsa.h"

/*
 * TLS 1.2 (RFC 5246), client and server, for Remote Desktop: Windows 10
 * and older Windows Servers speak TLS 1.2 on RDP, never 1.3 (net/tls.c,
 * the HTTPS client, is 1.3 only and stays as it is).
 *
 * One cipher suite family: ECDHE with X25519 (RFC 8422 / 7748) and
 * AES-128-GCM (RFC 5288) - TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 and,
 * as a client, its ECDSA twin. Extended master secret (RFC 7627) and
 * secure renegotiation's empty renegotiation_info (RFC 5746) are spoken;
 * renegotiation itself is refused.
 *
 * As a client the server's certificate is NOT validated (Banana OS has no
 * CA store) - like tls.c. Remote Desktop's NLA ties the login to the
 * server's public key (tls12_peer_pubkey), which is what it checks.
 * As a server the identity is an RSA-2048 key with a self-signed
 * certificate (tls12_make_cert).
 */

typedef struct tls12_conn tls12_conn_t;

typedef struct {
    const rsa_key_t* key;
    const uint8_t*   cert;          /* DER */
    uint32_t         cert_len;
} tls12_identity_t;

/* On failure: NULL, *err (NET_ERR_*) and a reason in errmsg. The TCP
 * connection stays the caller's either way. */
tls12_conn_t* tls12_connect(tcp_conn_t* tcp, const char* server_name, uint32_t timeout_ms,
                            int* err, char* errmsg, uint32_t errmsg_len);
tls12_conn_t* tls12_accept(tcp_conn_t* tcp, const tls12_identity_t* id, uint32_t timeout_ms,
                           int* err, char* errmsg, uint32_t errmsg_len);

/* >0 bytes, 0 at the end of the stream, NET_ERR_TIMEOUT when nothing came
 * within timeout_ms (0: only what is there already), other NET_ERR_* */
int  tls12_read(tls12_conn_t* t, void* buf, uint32_t max, uint32_t timeout_ms);
/* 1 if tls12_read would return data (or an error) without waiting */
int  tls12_pending(tls12_conn_t* t);
int  tls12_write(tls12_conn_t* t, const void* data, uint32_t len, uint32_t timeout_ms);
/* close_notify, then frees the TLS state (not the TCP connection) */
void tls12_close(tls12_conn_t* t);

/* the peer's certificate (client side) and its SubjectPublicKey (the
 * BIT STRING's contents - what CredSSP binds the login to); 0 = ok */
int  tls12_peer_cert(tls12_conn_t* t, const uint8_t** der, uint32_t* len);
int  tls12_peer_pubkey(tls12_conn_t* t, const uint8_t** key, uint32_t* len);

/* a self-signed X.509 v3 certificate (sha256WithRSAEncryption) for
 * `key`, subject CN = common_name, valid 2025-2049: DER length, or -1 */
int  tls12_make_cert(const rsa_key_t* key, const char* common_name, uint8_t* out, uint32_t cap);

/* SHA-256 of a DER certificate as "AB:CD:..." (95 chars + NUL) */
void tls12_fingerprint(const uint8_t* der, uint32_t len, char out[96]);

/* TLS 1.2's PRF with SHA-256 (exposed for the known-answer test) */
void tls12_prf(const uint8_t* secret, uint32_t slen, const char* label, const uint8_t* seed, uint32_t seed_len,
               uint8_t* out, uint32_t out_len);

#endif
