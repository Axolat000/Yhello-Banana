/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MD5_H
#define MD5_H

#include "types.h"

/*
 * MD4 (RFC 1320), MD5 (RFC 1321), HMAC-MD5 (RFC 2104) and RC4 - old and
 * broken as general-purpose primitives, but NTLM (the login of Remote
 * Desktop's NLA, net/ntlm.c) is made of exactly these. Nothing else
 * should use them.
 */

#define MD5_LEN 16

typedef struct {
    uint32_t h[4];
    uint64_t total;
    uint8_t  buf[64];
    uint32_t buf_len;
} md5_ctx_t;

void md5_init(md5_ctx_t* c);
void md5_update(md5_ctx_t* c, const void* data, uint32_t len);
void md5_final(md5_ctx_t* c, uint8_t out[MD5_LEN]);
void md5(const void* data, uint32_t len, uint8_t out[MD5_LEN]);

/* MD4 in one go (NTLM hashes a password with it) */
void md4(const void* data, uint32_t len, uint8_t out[MD5_LEN]);

typedef struct {
    md5_ctx_t inner, outer;
} hmac_md5_ctx_t;

void hmac_md5_init(hmac_md5_ctx_t* c, const uint8_t* key, uint32_t key_len);
void hmac_md5_update(hmac_md5_ctx_t* c, const void* data, uint32_t len);
void hmac_md5_final(hmac_md5_ctx_t* c, uint8_t out[MD5_LEN]);
void hmac_md5(const uint8_t* key, uint32_t key_len, const void* data, uint32_t len, uint8_t out[MD5_LEN]);

typedef struct {
    uint8_t s[256];
    uint8_t i, j;
} rc4_t;

void rc4_init(rc4_t* r, const uint8_t* key, uint32_t key_len);
/* in place; the stream goes on across calls */
void rc4_crypt(rc4_t* r, uint8_t* buf, uint32_t len);

#endif
