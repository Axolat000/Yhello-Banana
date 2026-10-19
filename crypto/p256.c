/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "p256.h"

/*
 * NIST P-256 (secp256r1) ECDH - see p256.h. Field elements are 8 little-
 * endian 32-bit limbs in the Montgomery domain; multiplication is CIOS
 * Montgomery (Koc). Points are Jacobian (X, Y, Z); scalar multiplication
 * is left-to-right double-and-add. Not constant time - good enough for a
 * TLS key exchange on a hobby OS, like the rest of crypto/.
 */

typedef uint32_t fe[8];

/* p = 2^256 - 2^224 + 2^192 + 2^96 - 1, little-endian limbs */
static const uint32_t P[8] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
                               0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF };
#define N0 1u                       /* -P[0]^{-1} mod 2^32 (P[0] = 2^32-1) */

/* R^2 mod p, for converting into the Montgomery domain (big-endian below) */
static const uint32_t R2[8] = { 0x00000003, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFB,
                                0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFD, 0x00000004 };
static const uint32_t ONE[8]  = { 1, 0, 0, 0, 0, 0, 0, 0 };

static int fe_geq_p(const uint32_t a[8]) {           /* a >= p ? */
    for (int i = 7; i >= 0; i--) { if (a[i] != P[i]) return a[i] > P[i]; }
    return 1;
}
static void fe_sub_p(uint32_t a[8]) {                /* a -= p (a >= p) */
    uint64_t b = 0;
    for (int i = 0; i < 8; i++) { uint64_t s = (uint64_t)a[i] - P[i] - b; a[i] = (uint32_t)s; b = (s >> 63) & 1; }
}

static void fe_add(fe r, const fe a, const fe b) {
    uint64_t c = 0;
    for (int i = 0; i < 8; i++) { uint64_t s = (uint64_t)a[i] + b[i] + c; r[i] = (uint32_t)s; c = s >> 32; }
    if (c || fe_geq_p(r)) fe_sub_p(r);
}
static void fe_sub(fe r, const fe a, const fe b) {
    uint64_t bo = 0;
    for (int i = 0; i < 8; i++) { uint64_t s = (uint64_t)a[i] - b[i] - bo; r[i] = (uint32_t)s; bo = (s >> 63) & 1; }
    if (bo) { uint64_t c = 0; for (int i = 0; i < 8; i++) { uint64_t s = (uint64_t)r[i] + P[i] + c; r[i] = (uint32_t)s; c = s >> 32; } }
}

/* CIOS Montgomery multiplication: r = a*b*R^-1 mod p */
static void fe_mul(fe r, const fe a, const fe b) {
    uint32_t t[10] = { 0 };
    for (int i = 0; i < 8; i++) {
        uint64_t c = 0, s;
        for (int j = 0; j < 8; j++) { s = (uint64_t)t[j] + (uint64_t)a[j] * b[i] + c; t[j] = (uint32_t)s; c = s >> 32; }
        s = (uint64_t)t[8] + c; t[8] = (uint32_t)s; t[9] = (uint32_t)(s >> 32);
        uint32_t m = t[0] * N0;
        s = (uint64_t)t[0] + (uint64_t)m * P[0]; c = s >> 32;
        for (int j = 1; j < 8; j++) { s = (uint64_t)t[j] + (uint64_t)m * P[j] + c; t[j - 1] = (uint32_t)s; c = s >> 32; }
        s = (uint64_t)t[8] + c; t[7] = (uint32_t)s; c = s >> 32;
        t[8] = t[9] + (uint32_t)c;
    }
    for (int i = 0; i < 8; i++) r[i] = t[i];
    if (t[8] || fe_geq_p(r)) fe_sub_p(r);
}
static void fe_sqr(fe r, const fe a) { fe_mul(r, a, a); }
static void fe_set(fe r, const fe a) { for (int i = 0; i < 8; i++) r[i] = a[i]; }
static void fe_to_mont(fe r, const fe a) { fe_mul(r, a, R2); }
static void fe_from_mont(fe r, const fe a) { fe_mul(r, a, ONE); }
static int  fe_is_zero(const fe a) { uint32_t x = 0; for (int i = 0; i < 8; i++) x |= a[i]; return x == 0; }
static int  fe_eq(const fe a, const fe b) { for (int i = 0; i < 8; i++) if (a[i] != b[i]) return 0; return 1; }

/* a^-1 mod p = a^(p-2); p-2 little-endian limbs */
static void fe_inv(fe r, const fe a) {
    static const uint32_t PM2[8] = { 0xFFFFFFFD, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
                                     0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF };
    fe x;
    fe_set(x, a);
    fe_to_mont(r, ONE);      /* r = 1 (Montgomery domain) */
    for (int i = 255; i >= 0; i--) {
        fe_sqr(r, r);
        if ((PM2[i >> 5] >> (i & 31)) & 1) fe_mul(r, r, x);
    }
}

/* ── big-endian <-> limbs ─────────────────────────────────────────── */
static void be_to_fe(fe r, const uint8_t b[32]) {
    for (int i = 0; i < 8; i++)
        r[i] = (uint32_t)b[31 - 4 * i] | (uint32_t)b[30 - 4 * i] << 8 | (uint32_t)b[29 - 4 * i] << 16 | (uint32_t)b[28 - 4 * i] << 24;
}
static void fe_to_be(uint8_t b[32], const fe a) {
    for (int i = 0; i < 8; i++) {
        b[31 - 4 * i] = (uint8_t)a[i];
        b[30 - 4 * i] = (uint8_t)(a[i] >> 8);
        b[29 - 4 * i] = (uint8_t)(a[i] >> 16);
        b[28 - 4 * i] = (uint8_t)(a[i] >> 24);
    }
}

/* ── curve constants (Montgomery domain, filled on first use) ─────── */
typedef struct { fe X, Y, Z; } jac;
static fe B_m;                       /* b */
static jac G_m;                      /* the base point */
static int g_ready;

static void p256_init(void) {
    if (g_ready) return;
    static const uint8_t Bb[32] = { 0x5a,0xc6,0x35,0xd8,0xaa,0x3a,0x93,0xe7,0xb3,0xeb,0xbd,0x55,0x76,0x98,0x86,0xbc,
                                    0x65,0x1d,0x06,0xb0,0xcc,0x53,0xb0,0xf6,0x3b,0xce,0x3c,0x3e,0x27,0xd2,0x60,0x4b };
    static const uint8_t Gx[32] = { 0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
                                    0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96 };
    static const uint8_t Gy[32] = { 0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
                                    0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5 };
    fe t;
    be_to_fe(t, Bb); fe_to_mont(B_m, t);
    be_to_fe(t, Gx); fe_to_mont(G_m.X, t);
    be_to_fe(t, Gy); fe_to_mont(G_m.Y, t);
    fe_to_mont(G_m.Z, ONE);
    g_ready = 1;
}

/* ── point arithmetic (Jacobian, a = -3) ──────────────────────────── */

static void jac_double(jac* r, const jac* p) {
    if (fe_is_zero(p->Z)) { fe_set(r->Z, p->Z); return; }
    fe delta, gamma, beta, alpha, t1, t2, three;
    fe_to_mont(three, (const uint32_t[8]){ 3, 0, 0, 0, 0, 0, 0, 0 });
    fe_sqr(delta, p->Z);
    fe_sqr(gamma, p->Y);
    fe_mul(beta, p->X, gamma);
    fe_sub(t1, p->X, delta);
    fe_add(t2, p->X, delta);
    fe_mul(t1, t1, t2);
    fe_mul(alpha, t1, three);                        /* 3(X-Z^2)(X+Z^2) */
    fe X3, Y3, Z3, eight_beta, t3;
    fe_sqr(X3, alpha);
    fe_add(t3, beta, beta); fe_add(t3, t3, t3); fe_add(eight_beta, t3, t3);   /* 8 beta */
    fe_sub(X3, X3, eight_beta);
    fe_add(Z3, p->Y, p->Z); fe_sqr(Z3, Z3); fe_sub(Z3, Z3, gamma); fe_sub(Z3, Z3, delta);
    fe_add(t3, beta, beta); fe_add(t3, t3, t3);      /* 4 beta */
    fe_sub(t3, t3, X3);
    fe_mul(Y3, alpha, t3);
    fe_sqr(t3, gamma); fe_add(t3, t3, t3); fe_add(t3, t3, t3); fe_add(t3, t3, t3);   /* 8 gamma^2 */
    fe_sub(Y3, Y3, t3);
    fe_set(r->X, X3); fe_set(r->Y, Y3); fe_set(r->Z, Z3);
}

static void jac_add(jac* r, const jac* p, const jac* q) {
    if (fe_is_zero(p->Z)) { *r = *q; return; }
    if (fe_is_zero(q->Z)) { *r = *p; return; }
    fe z1z1, z2z2, u1, u2, s1, s2, t;
    fe_sqr(z1z1, p->Z);
    fe_sqr(z2z2, q->Z);
    fe_mul(u1, p->X, z2z2);
    fe_mul(u2, q->X, z1z1);
    fe_mul(s1, p->Y, q->Z); fe_mul(s1, s1, z2z2);
    fe_mul(s2, q->Y, p->Z); fe_mul(s2, s2, z1z1);
    if (fe_eq(u1, u2)) {
        if (fe_eq(s1, s2)) { jac_double(r, p); return; }
        for (int i = 0; i < 8; i++) r->Z[i] = 0;     /* infinity */
        return;
    }
    fe h, i2, j, rr, v, X3, Y3, Z3;
    fe_sub(h, u2, u1);
    fe_add(i2, h, h); fe_sqr(i2, i2);                /* (2H)^2 */
    fe_mul(j, h, i2);
    fe_sub(rr, s2, s1); fe_add(rr, rr, rr);          /* 2(S2-S1) */
    fe_mul(v, u1, i2);
    fe_sqr(X3, rr); fe_sub(X3, X3, j); fe_sub(X3, X3, v); fe_sub(X3, X3, v);
    fe_sub(t, v, X3); fe_mul(Y3, rr, t);
    fe_mul(t, s1, j); fe_add(t, t, t);               /* 2 S1 J */
    fe_sub(Y3, Y3, t);
    fe_add(Z3, p->Z, q->Z); fe_sqr(Z3, Z3); fe_sub(Z3, Z3, z1z1); fe_sub(Z3, Z3, z2z2); fe_mul(Z3, Z3, h);
    fe_set(r->X, X3); fe_set(r->Y, Y3); fe_set(r->Z, Z3);
}

/* r = k * P, k big-endian 32 bytes */
static void scalar_mult(jac* r, const uint8_t k[32], const jac* P0) {
    jac acc;
    for (int i = 0; i < 8; i++) acc.Z[i] = 0;        /* infinity */
    for (int i = 0; i < 256; i++) {
        jac_double(&acc, &acc);
        int byte = k[i >> 3], bit = (byte >> (7 - (i & 7))) & 1;
        if (bit) jac_add(&acc, &acc, P0);
    }
    *r = acc;
}

/* Jacobian -> affine big-endian x, y */
static void jac_affine(uint8_t x[32], uint8_t y[32], const jac* p) {
    fe zi, zi2, zi3, xa, ya;
    fe_inv(zi, p->Z);
    fe_sqr(zi2, zi);
    fe_mul(zi3, zi2, zi);
    fe_mul(xa, p->X, zi2);
    fe_mul(ya, p->Y, zi3);
    fe_from_mont(xa, xa);
    fe_from_mont(ya, ya);
    fe_to_be(x, xa);
    if (y) fe_to_be(y, ya);
}

/* is the affine point (x, y) on y^2 = x^3 - 3x + b ? */
static int on_curve(const fe xm, const fe ym) {
    fe lhs, rhs, t, three;
    fe_to_mont(three, (const uint32_t[8]){ 3, 0, 0, 0, 0, 0, 0, 0 });
    fe_sqr(lhs, ym);
    fe_sqr(rhs, xm); fe_mul(rhs, rhs, xm);           /* x^3 */
    fe_mul(t, three, xm);                            /* 3x */
    fe_sub(rhs, rhs, t);
    fe_add(rhs, rhs, B_m);
    return fe_eq(lhs, rhs);
}

/* ── public API ───────────────────────────────────────────────────── */

void p256_public(uint8_t out[65], const uint8_t priv[32]) {
    p256_init();
    jac r;
    scalar_mult(&r, priv, &G_m);
    out[0] = 0x04;
    jac_affine(out + 1, out + 33, &r);
}

int p256_ecdh(uint8_t out[32], const uint8_t priv[32], const uint8_t peer[65]) {
    p256_init();
    if (peer[0] != 0x04) return -1;
    fe px, py, pxm, pym;
    be_to_fe(px, peer + 1);
    be_to_fe(py, peer + 33);
    if (fe_geq_p(px) || fe_geq_p(py)) return -1;
    fe_to_mont(pxm, px);
    fe_to_mont(pym, py);
    if (!on_curve(pxm, pym)) return -1;
    jac Pp, r;
    fe_set(Pp.X, pxm); fe_set(Pp.Y, pym); fe_to_mont(Pp.Z, ONE);
    scalar_mult(&r, priv, &Pp);
    if (fe_is_zero(r.Z)) return -1;                  /* shared point at infinity */
    jac_affine(out, 0, &r);
    return 0;
}
