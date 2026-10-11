#include "rsa.h"
#include "kstring.h"

/*
 * Big numbers as little-endian arrays of 32-bit words, of a fixed length
 * per operation (32 words for the primes, 64 for the modulus). Products
 * modulo m go through Montgomery multiplication (CIOS); exponentiation is
 * square-and-multiply. The work buffers are static (a small kernel stack
 * would not hold them): one RSA operation at a time. Not constant-time: fine for adb's own key on the
 * user's own computer, nothing else uses it.
 */

#define MAXW RSA_WORDS

typedef struct {
    int      nw;
    uint32_t m[MAXW];
    uint32_t m0inv;                        /* -m^-1 mod 2^32 */
    uint32_t rr[MAXW];                     /* R^2 mod m, R = 2^(32 nw) */
} mont_t;

static int cmp(const uint32_t* a, const uint32_t* b, int n) {
    for (int i = n - 1; i >= 0; i--) if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}

static uint32_t sub(uint32_t* r, const uint32_t* a, const uint32_t* b, int n) {
    uint64_t borrow = 0;
    for (int i = 0; i < n; i++) {
        uint64_t t = (uint64_t)a[i] - b[i] - borrow;
        r[i] = (uint32_t)t;
        borrow = (t >> 63) & 1;
    }
    return (uint32_t)borrow;
}

static int is_zero(const uint32_t* a, int n) {
    for (int i = 0; i < n; i++) if (a[i]) return 0;
    return 1;
}

static void mont_setup(mont_t* M, const uint32_t* m, int nw) {
    M->nw = nw;
    memcpy(M->m, m, (uint32_t)nw * 4);
    uint32_t x = 1;                        /* m^-1 mod 2^32 by Newton's iteration */
    for (int i = 0; i < 5; i++) x *= 2 - m[0] * x;
    M->m0inv = (uint32_t)(0 - x);
    /* R^2 mod m: 1 doubled 2 * 32 * nw times, reduced each time */
    static uint32_t r[MAXW + 1];
    memset(r, 0, sizeof(r));
    r[0] = 1;
    for (int k = 0; k < 64 * nw; k++) {
        uint32_t carry = 0;
        for (int i = 0; i < nw; i++) {
            uint32_t v = r[i];
            r[i] = (v << 1) | carry;
            carry = v >> 31;
        }
        if (carry || cmp(r, m, nw) >= 0) sub(r, r, m, nw);
    }
    memcpy(M->rr, r, (uint32_t)nw * 4);
}

/* r = a * b / R mod m */
static void mont_mul(const mont_t* M, uint32_t* r, const uint32_t* a, const uint32_t* b) {
    int n = M->nw;
    static uint32_t t[MAXW + 2];
    memset(t, 0, (uint32_t)(n + 2) * 4);
    for (int i = 0; i < n; i++) {
        uint64_t c = 0;
        for (int j = 0; j < n; j++) {
            uint64_t s = (uint64_t)a[j] * b[i] + t[j] + c;
            t[j] = (uint32_t)s;
            c = s >> 32;
        }
        uint64_t s = (uint64_t)t[n] + c;
        t[n] = (uint32_t)s;
        t[n + 1] = (uint32_t)(s >> 32);
        uint32_t u = t[0] * M->m0inv;
        s = (uint64_t)u * M->m[0] + t[0];
        c = s >> 32;
        for (int j = 1; j < n; j++) {
            s = (uint64_t)u * M->m[j] + t[j] + c;
            t[j - 1] = (uint32_t)s;
            c = s >> 32;
        }
        s = (uint64_t)t[n] + c;
        t[n - 1] = (uint32_t)s;
        t[n] = t[n + 1] + (uint32_t)(s >> 32);
    }
    if (t[n] || cmp(t, M->m, n) >= 0) sub(t, t, M->m, n);
    memcpy(r, t, (uint32_t)n * 4);
}

/* r = b^e mod m (e: ew words) */
static void mod_exp(const mont_t* M, uint32_t* r, const uint32_t* b, const uint32_t* e, int ew) {
    int n = M->nw;
    static uint32_t x[MAXW], acc[MAXW], one[MAXW], bb[MAXW];
    memset(one, 0, sizeof(one));
    one[0] = 1;
    memcpy(bb, b, (uint32_t)n * 4);
    if (cmp(bb, M->m, n) >= 0) sub(bb, bb, M->m, n);   /* (b < 2m here) */
    mont_mul(M, x, bb, M->rr);             /* b in Montgomery form */
    mont_mul(M, acc, one, M->rr);          /* 1 in Montgomery form */
    int top = ew * 32 - 1;
    while (top >= 0 && !((e[top / 32] >> (top % 32)) & 1)) top--;
    for (int i = top; i >= 0; i--) {
        mont_mul(M, acc, acc, acc);
        if ((e[i / 32] >> (i % 32)) & 1) mont_mul(M, acc, acc, x);
    }
    mont_mul(M, r, acc, one);              /* out of Montgomery form */
}

/* a mod small */
static uint32_t mod_small(const uint32_t* a, int n, uint32_t m) {
    uint64_t r = 0;
    for (int i = n - 1; i >= 0; i--) r = ((r << 32) | a[i]) % m;
    return (uint32_t)r;
}

static const uint16_t SMALL_PRIMES[] = {
    3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97,
    101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193,
    197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307,
    311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421,
    431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547,
    557, 563, 569, 571, 577, 587, 593, 599, 601, 607, 613, 617, 619, 631, 641, 643, 647, 653, 659,
    661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797,
    809, 811, 821, 823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919, 929,
    937, 941, 947, 953, 967, 971, 977, 983, 991, 997,
};

/* Miller-Rabin with `rounds` random bases */
static int probably_prime(const uint32_t* p, int n, int rounds, void (*rnd)(void*, uint32_t)) {
    static mont_t M;
    mont_setup(&M, p, n);
    static uint32_t d[MAXW], pm1[MAXW], one[MAXW];
    memset(one, 0, sizeof(one));
    one[0] = 1;
    sub(pm1, p, one, n);
    memcpy(d, pm1, (uint32_t)n * 4);
    int s = 0;
    while (!(d[0] & 1)) {                  /* p - 1 = d * 2^s */
        for (int i = 0; i < n; i++) d[i] = (d[i] >> 1) | (i + 1 < n ? d[i + 1] << 31 : 0);
        s++;
    }
    for (int r = 0; r < rounds; r++) {
        static uint32_t a[MAXW], x[MAXW];
        rnd(a, (uint32_t)n * 4);
        a[n - 1] &= 0x3FFFFFFF;            /* below p (whose top bits are set) */
        a[0] |= 2;
        mod_exp(&M, x, a, d, n);
        if (!cmp(x, one, n) || !cmp(x, pm1, n)) continue;
        int ok = 0;
        for (int i = 1; i < s && !ok; i++) {
            static uint32_t xm[MAXW];
            mont_mul(&M, xm, x, x);        /* x^2 / R */
            mont_mul(&M, x, xm, M.rr);     /* x^2 mod p */
            if (!cmp(x, pm1, n)) ok = 1;
        }
        if (!ok) return 0;
    }
    return 1;
}

/* a random prime of 32 * n bits (the top two bits set: p * q has 64n bits),
 * p mod e != 1 */
static void gen_prime(uint32_t* p, int n, uint32_t e, void (*rnd)(void*, uint32_t)) {
    for (;;) {
        rnd(p, (uint32_t)n * 4);
        p[n - 1] |= 0xC0000000u;
        p[0] |= 1;
        /* walk up by 2 from there, skipping candidates small primes divide */
        for (int step = 0; step < 4096; step++) {
            int bad = mod_small(p, n, e) == 1;
            for (unsigned k = 0; !bad && k < sizeof(SMALL_PRIMES) / sizeof(SMALL_PRIMES[0]); k++)
                if (mod_small(p, n, SMALL_PRIMES[k]) == 0) bad = 1;
            if (!bad && probably_prime(p, n, 1, rnd) && probably_prime(p, n, 24, rnd)) return;
            uint64_t c = 2;                /* p += 2 */
            for (int i = 0; i < n && c; i++) { c += p[i]; p[i] = (uint32_t)c; c >>= 32; }
            if (!(p[n - 1] & 0x80000000u)) break;   /* (wrapped: a new start) */
        }
    }
}

/* r (2n words) = a * b (n words each) */
static void mul_full(uint32_t* r, const uint32_t* a, const uint32_t* b, int n) {
    memset(r, 0, (uint32_t)n * 8);
    for (int i = 0; i < n; i++) {
        uint64_t c = 0;
        for (int j = 0; j < n; j++) {
            uint64_t s = (uint64_t)a[i] * b[j] + r[i + j] + c;
            r[i + j] = (uint32_t)s;
            c = s >> 32;
        }
        r[i + n] = (uint32_t)c;
    }
}

/* small modular inverse: a^-1 mod m (gcd 1) */
static uint32_t inv_small(uint32_t a, uint32_t m) {
    int64_t t = 0, nt = 1, r = m, nr = a % m;
    while (nr) {
        int64_t q = r / nr, tmp;
        tmp = t - q * nt; t = nt; nt = tmp;
        tmp = r - q * nr; r = nr; nr = tmp;
    }
    if (t < 0) t += m;
    return (uint32_t)t;
}

int rsa_generate(rsa_key_t* k, void (*rnd)(void* buf, uint32_t len)) {
    const int h = RSA_WORDS / 2;
    static uint32_t p[MAXW], q[MAXW];
    memset(k, 0, sizeof(*k));
    k->e = 65537;
    memset(p, 0, sizeof(p));
    memset(q, 0, sizeof(q));
    gen_prime(p, h, k->e, rnd);
    do gen_prime(q, h, k->e, rnd); while (!cmp(p, q, h));
    mul_full(k->n, p, q, h);
    /* phi = (p - 1)(q - 1); d = (1 + j phi) / e with j phi = -1 (mod e) */
    static uint32_t one[MAXW], pm1[MAXW], qm1[MAXW], phi[MAXW + 1];
    memset(one, 0, sizeof(one));
    one[0] = 1;
    sub(pm1, p, one, h);
    sub(qm1, q, one, h);
    memset(phi, 0, sizeof(phi));
    mul_full(phi, pm1, qm1, h);
    uint32_t pe = mod_small(phi, RSA_WORDS, k->e);
    if (!pe) return -1;
    uint32_t j = k->e - inv_small(pe, k->e);
    if (j == k->e) j = 0;
    uint64_t c = 0;                        /* phi * j + 1, then / e */
    static uint32_t t[MAXW + 1];
    for (int i = 0; i < RSA_WORDS; i++) {
        uint64_t s = (uint64_t)phi[i] * j + c;
        t[i] = (uint32_t)s;
        c = s >> 32;
    }
    t[RSA_WORDS] = (uint32_t)c;
    c = 1;
    for (int i = 0; i <= RSA_WORDS && c; i++) { c += t[i]; t[i] = (uint32_t)c; c >>= 32; }
    uint64_t rem = 0;
    for (int i = RSA_WORDS; i >= 0; i--) {
        uint64_t cur = (rem << 32) | t[i];
        t[i] = (uint32_t)(cur / k->e);
        rem = cur % k->e;
    }
    if (rem || t[RSA_WORDS]) return -1;
    memcpy(k->d, t, sizeof(k->d));
    memset(p, 0, sizeof(p));
    memset(q, 0, sizeof(q));
    return 0;
}

static void from_be(uint32_t* w, const uint8_t* b) {
    for (int i = 0; i < RSA_WORDS; i++) {
        const uint8_t* s = b + RSA_BYTES - 4 * (i + 1);
        w[i] = (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3];
    }
}

static void to_be(uint8_t* b, const uint32_t* w) {
    for (int i = 0; i < RSA_WORDS; i++) {
        uint8_t* s = b + RSA_BYTES - 4 * (i + 1);
        s[0] = (uint8_t)(w[i] >> 24); s[1] = (uint8_t)(w[i] >> 16); s[2] = (uint8_t)(w[i] >> 8); s[3] = (uint8_t)w[i];
    }
}

void rsa_sign_sha1(const rsa_key_t* k, const uint8_t digest[20], uint8_t sig[RSA_BYTES]) {
    static const uint8_t SHA1_INFO[15] = { 0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14 };
    static uint8_t em[RSA_BYTES];
    int tlen = 15 + 20;
    em[0] = 0x00;
    em[1] = 0x01;
    memset(em + 2, 0xFF, (uint32_t)(RSA_BYTES - 3 - tlen));
    em[RSA_BYTES - tlen - 1] = 0x00;
    memcpy(em + RSA_BYTES - tlen, SHA1_INFO, 15);
    memcpy(em + RSA_BYTES - 20, digest, 20);
    static uint32_t m[MAXW], s[MAXW];
    from_be(m, em);
    static mont_t M;
    mont_setup(&M, k->n, RSA_WORDS);
    mod_exp(&M, s, m, k->d, RSA_WORDS);
    to_be(sig, s);
}

void rsa_public(const rsa_key_t* k, const uint8_t in[RSA_BYTES], uint8_t out[RSA_BYTES]) {
    static uint32_t m[MAXW], s[MAXW];
    uint32_t e[1] = { k->e };
    from_be(m, in);
    static mont_t M;
    mont_setup(&M, k->n, RSA_WORDS);
    mod_exp(&M, s, m, e, 1);
    to_be(out, s);
}

static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

void rsa_android_pubkey(const rsa_key_t* k, uint8_t out[RSA_ANDROID_PUB_LEN]) {
    static mont_t M;
    mont_setup(&M, k->n, RSA_WORDS);
    put32(out, RSA_WORDS);
    put32(out + 4, M.m0inv);
    for (int i = 0; i < RSA_WORDS; i++) put32(out + 8 + 4 * i, k->n[i]);
    for (int i = 0; i < RSA_WORDS; i++) put32(out + 8 + 4 * RSA_WORDS + 4 * i, M.rr[i]);
    put32(out + 8 + 8 * RSA_WORDS, k->e);
    (void)is_zero;
}
