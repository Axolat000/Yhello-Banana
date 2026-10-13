#include "ntlm.h"
#include "kstring.h"
#include "random.h"
#include "utf8.h"

/* NTLM client - see ntlm.h. Section numbers: MS-NLMP. */

#define F_UNICODE        0x00000001u
#define F_REQUEST_TARGET 0x00000004u
#define F_SIGN           0x00000010u
#define F_SEAL           0x00000020u
#define F_NTLM           0x00000200u
#define F_ALWAYS_SIGN    0x00008000u
#define F_ESS            0x00080000u       /* extended session security */
#define F_TARGET_INFO    0x00800000u
#define F_VERSION        0x02000000u
#define F_128            0x20000000u
#define F_KEY_EXCH       0x40000000u
#define F_56             0x80000000u

#define OUR_FLAGS (F_56 | F_KEY_EXCH | F_128 | F_VERSION | F_ESS | F_ALWAYS_SIGN | F_NTLM | \
                   F_SEAL | F_SIGN | F_REQUEST_TARGET | F_UNICODE)

/* AV pairs (2.2.2.1) */
#define AV_EOL         0
#define AV_FLAGS       6
#define AV_TIMESTAMP   7
#define AV_TARGET_NAME 9
#define AV_BINDINGS    10

static const uint8_t SIGNATURE[8] = { 'N', 'T', 'L', 'M', 'S', 'S', 'P', 0 };
/* Windows 10, NTLM revision 15 (2.2.2.10) */
static const uint8_t VERSION[8] = { 10, 0, 0x61, 0x4A, 0, 0, 0, 15 };

static void le16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t* p, uint32_t v) { le16(p, v); le16(p + 2, v >> 16); }
static uint32_t rdle16(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rdle32(const uint8_t* p) { return rdle16(p) | rdle16(p + 2) << 16; }

uint32_t ntlm_utf16(const char* s, uint8_t* out, uint32_t cap) {
    int len = (int)strlen(s), i = 0;
    uint32_t o = 0;
    while (i < len) {
        uint32_t cp = u8_decode(s, len, &i);
        if (cp >= 0x10000) {                        /* a surrogate pair */
            if (o + 4 > cap) break;
            cp -= 0x10000;
            le16(out + o, 0xD800 | (cp >> 10));
            le16(out + o + 2, 0xDC00 | (cp & 0x3FF));
            o += 4;
        } else {
            if (o + 2 > cap) break;
            le16(out + o, cp);
            o += 2;
        }
    }
    return o;
}

/* NTOWFv2 (3.3.2): HMAC-MD5(MD4(password), UPPERCASE(user) || domain) */
void ntlm_ntowfv2(const uint8_t* pw16, uint32_t pw_len, const uint8_t* user16, uint32_t user_len,
                  const uint8_t* dom16, uint32_t dom_len, uint8_t out[16]) {
    uint8_t h[16], up[256];
    md4(pw16, pw_len, h);
    if (user_len > sizeof(up)) user_len = sizeof(up);
    for (uint32_t i = 0; i + 1 < user_len; i += 2) {
        uint32_t c = rdle16(user16 + i);
        if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) c -= 0x20;
        else if (c == 0xFF) c = 0x178;
        le16(up + i, c);
    }
    hmac_md5_ctx_t m;
    hmac_md5_init(&m, h, 16);
    hmac_md5_update(&m, up, user_len);
    hmac_md5_update(&m, dom16, dom_len);
    hmac_md5_final(&m, out);
    memset(h, 0, sizeof(h));
}

/* 3.3.2: temp = 01 01 Z(6) time client_challenge Z(4) av Z(4);
 * NTProofStr = HMAC-MD5(key, server_challenge || temp) */
uint32_t ntlm_v2_response(const uint8_t key[16], const uint8_t server_chal[8], const uint8_t client_chal[8],
                          const uint8_t time[8], const uint8_t* av, uint32_t av_len,
                          uint8_t* resp, uint8_t base_key[16]) {
    uint8_t* t = resp + 16;
    memset(t, 0, 28);
    t[0] = 1;
    t[1] = 1;
    memcpy(t + 8, time, 8);
    memcpy(t + 16, client_chal, 8);
    memcpy(t + 28, av, av_len);
    memset(t + 28 + av_len, 0, 4);
    uint32_t tl = 28 + av_len + 4;
    hmac_md5_ctx_t m;
    hmac_md5_init(&m, key, 16);
    hmac_md5_update(&m, server_chal, 8);
    hmac_md5_update(&m, t, tl);
    hmac_md5_final(&m, resp);
    hmac_md5(key, 16, resp, 16, base_key);          /* SessionBaseKey */
    return 16 + tl;
}

static void sub_key(const uint8_t exported[16], const char* magic, uint8_t out[16]) {
    md5_ctx_t c;
    md5_init(&c);
    md5_update(&c, exported, 16);
    md5_update(&c, magic, (uint32_t)strlen(magic) + 1);     /* the NUL is part of it */
    md5_final(&c, out);
}

/* 3.4.5.2 / 3.4.5.3: 128-bit keys (NTLMSSP_NEGOTIATE_128) */
void ntlm_keys(ntlm_t* n, const uint8_t exported[16]) {
    uint8_t k[16];
    memcpy(n->exported, exported, 16);
    sub_key(exported, "session key to client-to-server signing key magic constant", n->c_sign);
    sub_key(exported, "session key to server-to-client signing key magic constant", n->s_sign);
    sub_key(exported, "session key to client-to-server sealing key magic constant", k);
    rc4_init(&n->c_seal, k, 16);
    sub_key(exported, "session key to server-to-client sealing key magic constant", k);
    rc4_init(&n->s_seal, k, 16);
    memset(k, 0, sizeof(k));
    n->c_seq = n->s_seq = 0;
}

int ntlm_init(ntlm_t* n, const char* user, const char* domain, const char* password, const char* host) {
    memset(n, 0, sizeof(*n));
    n->user_len = ntlm_utf16(user, n->user, sizeof(n->user));
    n->domain_len = ntlm_utf16(domain ? domain : "", n->domain, sizeof(n->domain));
    n->password_len = ntlm_utf16(password, n->password, sizeof(n->password));
    ksnprintf(n->spn, sizeof(n->spn), "TERMSRV/%s", host ? host : "");
    return n->user_len ? 0 : -1;
}

uint32_t ntlm_negotiate(ntlm_t* n, uint8_t* out) {
    memset(out, 0, 40);
    memcpy(out, SIGNATURE, 8);
    le32(out + 8, 1);
    le32(out + 12, OUR_FLAGS);
    le32(out + 20, 40);                             /* domain, workstation: empty */
    le32(out + 28, 40);
    memcpy(out + 32, VERSION, 8);
    memcpy(n->nego, out, 40);
    n->nego_len = 40;
    return 40;
}

static void put_field(uint8_t* hdr, uint8_t* msg, uint32_t* off, const void* data, uint32_t len) {
    le16(hdr, len);
    le16(hdr + 2, len);
    le32(hdr + 4, *off);
    memcpy(msg + *off, data, len);
    *off += len;
}

int ntlm_authenticate(ntlm_t* n, const uint8_t* chal, uint32_t chal_len, uint8_t* out, char* err, int errcap) {
    if (chal_len < 32 || memcmp(chal, SIGNATURE, 8) != 0 || rdle32(chal + 8) != 2) {
        ksnprintf(err, (size_t)errcap, "the server's NTLM challenge is malformed");
        return -1;
    }
    uint32_t flags = rdle32(chal + 20);
    if (!(flags & F_ESS) || !(flags & F_128) || !(flags & F_NTLM)) {
        ksnprintf(err, (size_t)errcap, "the server wants an old NTLM variant (flags %08x)", flags);
        return -1;
    }
    n->flags = flags;
    const uint8_t* server_chal = chal + 24;

    /* the server's target info, ours made from it (3.1.5.1.2): its pairs,
     * then the MIC flag, empty channel bindings and the SPN */
    const uint8_t* ti = NULL;
    uint32_t ti_len = 0;
    if (chal_len >= 48) {
        ti_len = rdle16(chal + 40);
        uint32_t ti_off = rdle32(chal + 44);
        if (ti_len && ti_off + ti_len <= chal_len) ti = chal + ti_off;
        else ti_len = 0;
    }
    uint8_t av[600], time[8];
    uint32_t avl = 0, av_flags = 0;
    int have_time = 0;
    memset(time, 0, 8);
    for (uint32_t p = 0; ti && p + 4 <= ti_len;) {
        uint32_t id = rdle16(ti + p), l = rdle16(ti + p + 2);
        if (id == AV_EOL || p + 4 + l > ti_len) break;
        if (id == AV_TIMESTAMP && l == 8) { memcpy(time, ti + p + 4, 8); have_time = 1; }
        if (id == AV_FLAGS && l == 4) av_flags = rdle32(ti + p + 4);
        else if (id != AV_BINDINGS && id != AV_TARGET_NAME) {
            if (avl + 4 + l > sizeof(av) - 200) { ksnprintf(err, (size_t)errcap, "NTLM target info too long"); return -1; }
            memcpy(av + avl, ti + p, 4 + l);
            avl += 4 + l;
        }
        p += 4 + l;
    }
    if (have_time) av_flags |= 2;                   /* a MIC is coming */
    if (have_time || av_flags) {
        le16(av + avl, AV_FLAGS); le16(av + avl + 2, 4); le32(av + avl + 4, av_flags);
        avl += 8;
    }
    le16(av + avl, AV_BINDINGS); le16(av + avl + 2, 16); memset(av + avl + 4, 0, 16);
    avl += 20;
    uint8_t spn[160];
    uint32_t sl = ntlm_utf16(n->spn, spn, sizeof(spn));
    le16(av + avl, AV_TARGET_NAME); le16(av + avl + 2, sl); memcpy(av + avl + 4, spn, sl);
    avl += 4 + sl;
    le16(av + avl, AV_EOL); le16(av + avl + 2, 0);
    avl += 4;
    if (n->fixed) memcpy(time, n->fixed_time, 8);

    /* the responses (3.3.2) */
    uint8_t key[16], cc[8], base[16], lm[24];
    if (n->fixed) memcpy(cc, n->fixed_cc, 8); else random_bytes(cc, 8);
    ntlm_ntowfv2(n->password, n->password_len, n->user, n->user_len, n->domain, n->domain_len, key);
    uint8_t nt[16 + 28 + sizeof(av) + 4];
    uint32_t ntl = ntlm_v2_response(key, server_chal, cc, time, av, avl, nt, base);
    if (have_time) {
        memset(lm, 0, 24);                          /* with a timestamp: Z(24) */
    } else {
        hmac_md5_ctx_t m;
        hmac_md5_init(&m, key, 16);
        hmac_md5_update(&m, server_chal, 8);
        hmac_md5_update(&m, cc, 8);
        hmac_md5_final(&m, lm);
        memcpy(lm + 16, cc, 8);
    }
    memset(key, 0, sizeof(key));

    /* the session key: KeyExchangeKey = SessionBaseKey for NTLMv2 (3.4.5.1) */
    uint8_t exported[16], enc[16];
    uint32_t enc_len = 0;
    if (flags & F_KEY_EXCH) {
        if (n->fixed) memcpy(exported, n->fixed_key, 16); else random_bytes(exported, 16);
        memcpy(enc, exported, 16);
        rc4_t r;
        rc4_init(&r, base, 16);
        rc4_crypt(&r, enc, 16);
        memset(&r, 0, sizeof(r));
        enc_len = 16;
    } else {
        memcpy(exported, base, 16);
    }
    memset(base, 0, sizeof(base));

    /* AUTHENTICATE (2.2.1.3): 88-byte header (with version and MIC), then the payload */
    static const uint8_t WS[] = { 'B', 0, 'A', 0, 'N', 0, 'A', 0, 'N', 0, 'A', 0 };
    if (88 + n->domain_len + n->user_len + sizeof(WS) + 24 + ntl + enc_len > NTLM_MSG_MAX) {
        ksnprintf(err, (size_t)errcap, "NTLM message too long");
        return -1;
    }
    memset(out, 0, 88);
    memcpy(out, SIGNATURE, 8);
    le32(out + 8, 3);
    uint32_t off = 88;
    put_field(out + 28, out, &off, n->domain, n->domain_len);
    put_field(out + 36, out, &off, n->user, n->user_len);
    put_field(out + 44, out, &off, WS, sizeof(WS));
    put_field(out + 12, out, &off, lm, 24);
    put_field(out + 20, out, &off, nt, ntl);
    put_field(out + 52, out, &off, enc, enc_len);
    le32(out + 60, flags);
    memcpy(out + 64, VERSION, 8);
    if (have_time) {
        /* MIC = HMAC-MD5(exported, NEGOTIATE || CHALLENGE || AUTHENTICATE) */
        hmac_md5_ctx_t m;
        hmac_md5_init(&m, exported, 16);
        hmac_md5_update(&m, n->nego, n->nego_len);
        hmac_md5_update(&m, chal, chal_len);
        hmac_md5_update(&m, out, off);
        hmac_md5_final(&m, out + 72);
    }
    ntlm_keys(n, exported);
    memset(exported, 0, sizeof(exported));
    return (int)off;
}

/* the 16-byte signature (3.4.4.2): version 1, the first 8 bytes of
 * HMAC-MD5(signing key, seq || plaintext) - RC4'd with the sealing
 * stream, after the message itself, when keys were exchanged - and seq */
static void mac_of(const uint8_t* sign_key, uint32_t seq, const uint8_t* msg, uint32_t len, uint8_t mac[16]) {
    uint8_t s[4];
    le32(s, seq);
    hmac_md5_ctx_t m;
    hmac_md5_init(&m, sign_key, 16);
    hmac_md5_update(&m, s, 4);
    hmac_md5_update(&m, msg, len);
    hmac_md5_final(&m, mac);
}

static void finish_sig(ntlm_t* n, rc4_t* seal, uint32_t seq, uint8_t mac[16], uint8_t sig[16]) {
    if (n->flags & F_KEY_EXCH) rc4_crypt(seal, mac, 8);
    le32(sig, 1);
    memcpy(sig + 4, mac, 8);
    le32(sig + 12, seq);
}

void ntlm_seal(ntlm_t* n, const uint8_t* msg, uint32_t len, uint8_t* out) {
    uint8_t mac[16];
    mac_of(n->c_sign, n->c_seq, msg, len, mac);
    memmove(out + 16, msg, len);
    rc4_crypt(&n->c_seal, out + 16, len);
    finish_sig(n, &n->c_seal, n->c_seq, mac, out);
    n->c_seq++;
}

int ntlm_unseal(ntlm_t* n, uint8_t* in, uint32_t len, uint8_t** msg) {
    if (len < 16) return -1;
    uint8_t* m = in + 16;
    uint32_t ml = len - 16;
    rc4_crypt(&n->s_seal, m, ml);
    uint8_t mac[16], want[16];
    mac_of(n->s_sign, n->s_seq, m, ml, mac);
    finish_sig(n, &n->s_seal, n->s_seq, mac, want);
    n->s_seq++;
    uint8_t d = 0;
    for (int i = 0; i < 16; i++) d |= (uint8_t)(want[i] ^ in[i]);
    if (d) return -1;
    *msg = m;
    return (int)ml;
}

void ntlm_wipe(ntlm_t* n) {
    memset(n, 0, sizeof(*n));
}
