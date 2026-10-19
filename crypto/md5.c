/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "md5.h"
#include "kstring.h"

/* MD4 and MD5 share the padding and the little-endian layout; only the
 * compression functions differ. */

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ── MD5 ──────────────────────────────────────────────────────────── */

static const uint32_t K5[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
static const uint8_t R5[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static void md5_block(uint32_t h[4], const uint8_t* p) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++) m[i] = le32(p + 4 * i);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3 * i + 5) & 15; }
        else             { f = c ^ (b | ~d);       g = (7 * i) & 15; }
        uint32_t t = d;
        d = c;
        c = b;
        b = b + ROL(a + f + K5[i] + m[g], R5[i]);
        a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}

/* ── MD4 ──────────────────────────────────────────────────────────── */

static void md4_block(uint32_t h[4], const uint8_t* p) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = le32(p + 4 * i);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
    static const uint8_t s1[4] = { 3, 7, 11, 19 }, s2[4] = { 3, 5, 9, 13 }, s3[4] = { 3, 9, 11, 15 };
    static const uint8_t o3[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
    for (int i = 0; i < 16; i++) {
        uint32_t t = a + F(b, c, d) + x[i];
        a = d; d = c; c = b; b = ROL(t, s1[i & 3]);
    }
    for (int i = 0; i < 16; i++) {
        int k = (i & 3) * 4 + (i >> 2);
        uint32_t t = a + G(b, c, d) + x[k] + 0x5A827999u;
        a = d; d = c; c = b; b = ROL(t, s2[i & 3]);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t t = a + H(b, c, d) + x[o3[i]] + 0x6ED9EBA1u;
        a = d; d = c; c = b; b = ROL(t, s3[i & 3]);
    }
#undef F
#undef G
#undef H
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}

/* ── shared padding ───────────────────────────────────────────────── */

static void ctx_init(md5_ctx_t* c) {
    c->h[0] = 0x67452301u; c->h[1] = 0xefcdab89u; c->h[2] = 0x98badcfeu; c->h[3] = 0x10325476u;
    c->total = 0;
    c->buf_len = 0;
}

static void ctx_update(md5_ctx_t* c, const void* data, uint32_t len, void (*blk)(uint32_t*, const uint8_t*)) {
    const uint8_t* p = (const uint8_t*)data;
    c->total += len;
    while (len) {
        uint32_t n = 64 - c->buf_len;
        if (n > len) n = len;
        memcpy(c->buf + c->buf_len, p, n);
        c->buf_len += n;
        p += n;
        len -= n;
        if (c->buf_len == 64) { blk(c->h, c->buf); c->buf_len = 0; }
    }
}

static void ctx_final(md5_ctx_t* c, uint8_t out[16], void (*blk)(uint32_t*, const uint8_t*)) {
    uint64_t bits = c->total * 8;
    uint8_t pad = 0x80;
    ctx_update(c, &pad, 1, blk);
    pad = 0;
    while (c->buf_len != 56) ctx_update(c, &pad, 1, blk);
    uint8_t l[8];
    for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (8 * i));
    ctx_update(c, l, 8, blk);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(c->h[i] >> (8 * j));
}

void md5_init(md5_ctx_t* c) { ctx_init(c); }
void md5_update(md5_ctx_t* c, const void* data, uint32_t len) { ctx_update(c, data, len, md5_block); }
void md5_final(md5_ctx_t* c, uint8_t out[MD5_LEN]) { ctx_final(c, out, md5_block); }

void md5(const void* data, uint32_t len, uint8_t out[MD5_LEN]) {
    md5_ctx_t c;
    md5_init(&c);
    md5_update(&c, data, len);
    md5_final(&c, out);
}

void md4(const void* data, uint32_t len, uint8_t out[MD5_LEN]) {
    md5_ctx_t c;
    ctx_init(&c);
    ctx_update(&c, data, len, md4_block);
    ctx_final(&c, out, md4_block);
}

/* ── HMAC-MD5 ─────────────────────────────────────────────────────── */

void hmac_md5_init(hmac_md5_ctx_t* c, const uint8_t* key, uint32_t key_len) {
    uint8_t k[64], pad[64];
    memset(k, 0, 64);
    if (key_len > 64) md5(key, key_len, k);
    else memcpy(k, key, key_len);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    md5_init(&c->inner);
    md5_update(&c->inner, pad, 64);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    md5_init(&c->outer);
    md5_update(&c->outer, pad, 64);
    memset(k, 0, 64);
}

void hmac_md5_update(hmac_md5_ctx_t* c, const void* data, uint32_t len) { md5_update(&c->inner, data, len); }

void hmac_md5_final(hmac_md5_ctx_t* c, uint8_t out[MD5_LEN]) {
    uint8_t ih[16];
    md5_final(&c->inner, ih);
    md5_update(&c->outer, ih, 16);
    md5_final(&c->outer, out);
}

void hmac_md5(const uint8_t* key, uint32_t key_len, const void* data, uint32_t len, uint8_t out[MD5_LEN]) {
    hmac_md5_ctx_t c;
    hmac_md5_init(&c, key, key_len);
    hmac_md5_update(&c, data, len);
    hmac_md5_final(&c, out);
}

/* ── RC4 ──────────────────────────────────────────────────────────── */

void rc4_init(rc4_t* r, const uint8_t* key, uint32_t key_len) {
    for (int i = 0; i < 256; i++) r->s[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = (uint8_t)(j + r->s[i] + key[i % key_len]);
        uint8_t t = r->s[i]; r->s[i] = r->s[j]; r->s[j] = t;
    }
    r->i = r->j = 0;
}

void rc4_crypt(rc4_t* r, uint8_t* buf, uint32_t len) {
    uint8_t i = r->i, j = r->j;
    for (uint32_t n = 0; n < len; n++) {
        i++;
        j = (uint8_t)(j + r->s[i]);
        uint8_t t = r->s[i]; r->s[i] = r->s[j]; r->s[j] = t;
        buf[n] ^= r->s[(uint8_t)(r->s[i] + r->s[j])];
    }
    r->i = i;
    r->j = j;
}
