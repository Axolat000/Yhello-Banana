#include "credssp.h"
#include "ntlm.h"
#include "sha256.h"
#include "aead.h"
#include "kstring.h"
#include "kheap.h"
#include "random.h"
#include "serial.h"

/* CredSSP client - see credssp.h. Section numbers: MS-CSSP. */

#define OUR_VERSION 6
#define MSG_MAX     4096

/* ── DER writing: a buffer, and wrapping what was written since a mark ── */

typedef struct { uint8_t* p; uint32_t len, cap; int err; } wb_t;

static void wb_bytes(wb_t* w, const void* d, uint32_t n) {
    if (w->len + n > w->cap) { w->err = 1; return; }
    memcpy(w->p + w->len, d, n);
    w->len += n;
}

static void wb_wrap(wb_t* w, uint32_t mark, uint8_t tag) {
    if (w->err) return;
    uint32_t n = w->len - mark;
    uint8_t h[4];
    uint32_t hl = 0;
    h[hl++] = tag;
    if (n < 0x80) h[hl++] = (uint8_t)n;
    else if (n < 0x100) { h[hl++] = 0x81; h[hl++] = (uint8_t)n; }
    else { h[hl++] = 0x82; h[hl++] = (uint8_t)(n >> 8); h[hl++] = (uint8_t)n; }
    if (w->len + hl > w->cap) { w->err = 1; return; }
    memmove(w->p + mark + hl, w->p + mark, n);
    memcpy(w->p + mark, h, hl);
    w->len += hl;
}

/* [tag] OCTET STRING */
static void wb_octets(wb_t* w, uint8_t ctx_tag, const void* d, uint32_t n) {
    uint32_t m = w->len;
    wb_bytes(w, d, n);
    wb_wrap(w, m, 0x04);
    wb_wrap(w, m, ctx_tag);
}

/* TSRequest (2.2.1): version, [negoTokens], [authInfo], [pubKeyAuth], [clientNonce] */
static uint32_t build_request(uint8_t* out, uint32_t cap, const uint8_t* token, uint32_t tlen,
                              const uint8_t* auth, uint32_t alen, const uint8_t* pka, uint32_t plen,
                              const uint8_t* nonce) {
    wb_t w = { out, 0, cap, 0 };
    uint32_t m = w.len;
    { uint8_t v[3] = { 0x02, 0x01, OUR_VERSION }; wb_bytes(&w, v, 3); wb_wrap(&w, m, 0xA0); }
    if (token) {
        uint32_t t = w.len;
        wb_octets(&w, 0xA0, token, tlen);          /* NegoData: SEQUENCE OF SEQUENCE { [0] OCTET STRING } */
        wb_wrap(&w, t, 0x30);
        wb_wrap(&w, t, 0x30);
        wb_wrap(&w, t, 0xA1);
    }
    if (auth) wb_octets(&w, 0xA2, auth, alen);
    if (pka) wb_octets(&w, 0xA3, pka, plen);
    if (nonce) wb_octets(&w, 0xA5, nonce, 32);
    wb_wrap(&w, m, 0x30);
    return w.err ? 0 : w.len;
}

/* ── DER reading ──────────────────────────────────────────────────── */

static const uint8_t* der_next(const uint8_t* p, const uint8_t* end, uint8_t* tag, const uint8_t** c, uint32_t* cl) {
    if (end - p < 2) return NULL;
    *tag = p[0];
    uint32_t l = p[1];
    p += 2;
    if (l & 0x80) {
        int nb = (int)(l & 0x7F);
        if (nb < 1 || nb > 3 || end - p < nb) return NULL;
        l = 0;
        while (nb--) l = (l << 8) | *p++;
    }
    if (l > (uint32_t)(end - p)) return NULL;
    *c = p;
    *cl = l;
    return p + l;
}

typedef struct {
    uint32_t version;
    const uint8_t* token; uint32_t token_len;
    const uint8_t* pka;   uint32_t pka_len;
    int      has_error;
    uint32_t error;
} ts_t;

static int parse_request(const uint8_t* m, uint32_t len, ts_t* ts) {
    memset(ts, 0, sizeof(*ts));
    uint8_t tag;
    const uint8_t *c, *f, *v;
    uint32_t cl, fl, vl;
    if (!der_next(m, m + len, &tag, &c, &cl) || tag != 0x30) return -1;
    const uint8_t* end = c + cl;
    /* [n] EXPLICIT fields, each holding one value */
    for (const uint8_t* p = c; p < end;) {
        uint8_t ft;
        p = der_next(p, end, &ft, &f, &fl);
        if (!p) return -1;
        uint8_t it;
        const uint8_t* inner_end = der_next(f, f + fl, &it, &v, &vl);
        if (!inner_end) return -1;
        if (ft == 0xA0 && it == 0x02) {
            for (uint32_t i = 0; i < vl; i++) ts->version = ts->version << 8 | v[i];
        } else if (ft == 0xA1 && it == 0x30) {
            /* SEQUENCE OF: the first SEQUENCE { [0] OCTET STRING } */
            uint8_t t2, t3, t4;
            const uint8_t *s, *a, *o;
            uint32_t sl, al, ol;
            if (der_next(v, v + vl, &t2, &s, &sl) && t2 == 0x30 &&
                der_next(s, s + sl, &t3, &a, &al) && t3 == 0xA0 &&
                der_next(a, a + al, &t4, &o, &ol) && t4 == 0x04) {
                ts->token = o;
                ts->token_len = ol;
            }
        } else if (ft == 0xA3 && it == 0x04) {
            ts->pka = v;
            ts->pka_len = vl;
        } else if (ft == 0xA4 && it == 0x02) {
            ts->has_error = 1;
            for (uint32_t i = 0; i < vl; i++) ts->error = ts->error << 8 | v[i];
        }
    }
    return 0;
}

/* ── the exchange ─────────────────────────────────────────────────── */

static int send_msg(tls12_conn_t* t, const uint8_t* m, uint32_t len) {
    return len && tls12_write(t, m, len, 10000) == (int)len ? 0 : -1;
}

/* one whole TSRequest (a DER SEQUENCE) into buf: its length, or < 0 */
static int recv_msg(tls12_conn_t* t, uint8_t* buf, uint32_t cap, uint32_t timeout_ms) {
    uint32_t have = 0, need = 2;
    while (have < need) {
        int r = tls12_read(t, buf + have, need - have, timeout_ms);
        if (r <= 0) return r == 0 ? NET_ERR_CLOSED : r;
        have += (uint32_t)r;
        if (have == 2 && need == 2) {
            if (buf[0] != 0x30) return NET_ERR_PROTO;
            uint32_t l = buf[1];
            if (l & 0x80) {
                uint32_t nb = l & 0x7F;
                if (nb < 1 || nb > 3) return NET_ERR_PROTO;
                need = 2 + nb;
                while (have < need) {
                    r = tls12_read(t, buf + have, need - have, timeout_ms);
                    if (r <= 0) return r == 0 ? NET_ERR_CLOSED : r;
                    have += (uint32_t)r;
                }
                l = 0;
                for (uint32_t i = 0; i < nb; i++) l = l << 8 | buf[2 + i];
            }
            need += l;
            if (need > cap) return NET_ERR_PROTO;
        }
    }
    return (int)have;
}

static const char* status_text(uint32_t s) {
    switch (s) {
    case 0xC000006D: return "wrong user name or password";
    case 0xC0000064: return "no such user";
    case 0xC000006A: return "wrong password";
    case 0xC000006E: return "the account may not log in now (restrictions)";
    case 0xC000006F: return "the account may not log in at this time";
    case 0xC0000070: return "the account may not log in from this computer";
    case 0xC0000071: return "the password has expired";
    case 0xC0000072: return "the account is disabled";
    case 0xC0000193: return "the account has expired";
    case 0xC0000224: return "the password must be changed first";
    case 0xC0000234: return "the account is locked out";
    case 0xC0000015: return "the server refused the request";
    case 0xC000015B: return "the user may not log in remotely";
    default:         return "the server refused the login";
    }
}

/* the hashes of 3.1.5 (version 5 and later) */
static void binding_hash(const char* magic, const uint8_t nonce[32], const uint8_t* key, uint32_t klen,
                         uint8_t out[32]) {
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, magic, (uint32_t)strlen(magic) + 1);
    sha256_update(&c, nonce, 32);
    sha256_update(&c, key, klen);
    sha256_final(&c, out);
}

int credssp_login(tls12_conn_t* t, const char* user, const char* domain, const char* password,
                  const char* host, uint32_t timeout_ms, char* err, int errcap) {
    const uint8_t* pub;
    uint32_t pub_len;
    if (tls12_peer_pubkey(t, &pub, &pub_len) != 0 || pub_len > 1024) {
        ksnprintf(err, (size_t)errcap, "NLA: the server's certificate has no usable public key");
        return -1;
    }
    ntlm_t* n = (ntlm_t*)kzalloc(sizeof(ntlm_t));
    uint8_t* msg = (uint8_t*)kmalloc(MSG_MAX);
    uint8_t* tok = (uint8_t*)kmalloc(NTLM_MSG_MAX + 1100);
    int rc = -1;
    if (!n || !msg || !tok) { ksnprintf(err, (size_t)errcap, "out of memory"); goto out; }
    ntlm_init(n, user, domain, password, host);

    /* 1. NEGOTIATE */
    uint32_t tl = ntlm_negotiate(n, tok);
    if (send_msg(t, msg, build_request(msg, MSG_MAX, tok, tl, NULL, 0, NULL, 0, NULL)) != 0) {
        ksnprintf(err, (size_t)errcap, "NLA: could not send to the server");
        goto out;
    }

    /* 2. CHALLENGE */
    ts_t ts;
    int r = recv_msg(t, msg, MSG_MAX, timeout_ms);
    if (r < 0 || parse_request(msg, (uint32_t)r, &ts) != 0 || !ts.token) {
        ksnprintf(err, (size_t)errcap, r < 0 ? "NLA: the server closed the connection (%s)" : "NLA: bad answer from the server%s",
                  r < 0 ? net_strerror(r) : "");
        goto out;
    }
    uint32_t peer_version = ts.version;
    klog("credssp: server speaks version %u\n", peer_version);

    /* 3. AUTHENTICATE + pubKeyAuth (+ the nonce from version 5) */
    int al = ntlm_authenticate(n, ts.token, ts.token_len, tok, err, errcap);
    if (al < 0) goto out;
    uint8_t nonce[32], h[32];
    uint8_t* pka = tok + al;                        /* (after the token in the same buffer) */
    uint32_t pka_len;
    if (peer_version >= 5) {
        random_bytes(nonce, 32);
        binding_hash("CredSSP Client-To-Server Binding Hash", nonce, pub, pub_len, h);
        ntlm_seal(n, h, 32, pka);
        pka_len = 16 + 32;
    } else {
        ntlm_seal(n, pub, pub_len, pka);
        pka_len = 16 + pub_len;
    }
    uint32_t ml = build_request(msg, MSG_MAX, tok, (uint32_t)al, NULL, 0, pka, pka_len, peer_version >= 5 ? nonce : NULL);
    if (send_msg(t, msg, ml) != 0) { ksnprintf(err, (size_t)errcap, "NLA: could not send to the server"); goto out; }

    /* 4. the server's pubKeyAuth: proves it holds the TLS key and the login worked */
    r = recv_msg(t, msg, MSG_MAX, timeout_ms);
    if (r < 0) {
        ksnprintf(err, (size_t)errcap, "NLA: the server ended the connection - most likely a wrong user name or password");
        goto out;
    }
    if (parse_request(msg, (uint32_t)r, &ts) != 0) { ksnprintf(err, (size_t)errcap, "NLA: bad answer from the server"); goto out; }
    if (ts.has_error) {
        ksnprintf(err, (size_t)errcap, "NLA: %s (0x%08x)", status_text(ts.error), ts.error);
        goto out;
    }
    uint8_t* plain;
    int pl = ts.pka ? ntlm_unseal(n, (uint8_t*)ts.pka, ts.pka_len, &plain) : -1;
    int ok = 0;
    if (pl >= 0 && peer_version >= 5) {
        binding_hash("CredSSP Server-To-Client Binding Hash", nonce, pub, pub_len, h);
        ok = pl == 32 && ct_memcmp(plain, h, 32) == 0;
    } else if (pl >= 0) {
        ok = (uint32_t)pl == pub_len && (uint8_t)(plain[0] - 1) == pub[0] && memcmp(plain + 1, pub + 1, pub_len - 1) == 0;
    }
    if (!ok) {
        ksnprintf(err, (size_t)errcap, "NLA: the server could not prove it holds this TLS certificate's key");
        goto out;
    }

    /* 5. TSCredentials (2.2.1.2): password credentials, sealed */
    {
        uint8_t* c = tok;                           /* (reused: the token is no longer needed) */
        wb_t w = { c, 0, NTLM_MSG_MAX + 1100, 0 };
        uint32_t m0 = w.len;
        { uint8_t v[3] = { 0x02, 0x01, 0x01 }; wb_bytes(&w, v, 3); wb_wrap(&w, m0, 0xA0); }   /* credType: password */
        uint32_t m1 = w.len, m2 = w.len;
        wb_octets(&w, 0xA0, n->domain, n->domain_len);
        wb_octets(&w, 0xA1, n->user, n->user_len);
        wb_octets(&w, 0xA2, n->password, n->password_len);
        wb_wrap(&w, m2, 0x30);                      /* TSPasswordCreds */
        wb_wrap(&w, m1, 0x04);
        wb_wrap(&w, m1, 0xA1);
        wb_wrap(&w, m0, 0x30);
        if (w.err || w.len + 16 > NTLM_MSG_MAX + 1100) { ksnprintf(err, (size_t)errcap, "NLA: credentials too long"); goto out; }
        ntlm_seal(n, c, w.len, c);
        ml = build_request(msg, MSG_MAX, NULL, 0, c, w.len + 16, NULL, 0, NULL);
        memset(c, 0, w.len + 16);
        if (send_msg(t, msg, ml) != 0) { ksnprintf(err, (size_t)errcap, "NLA: could not send to the server"); goto out; }
    }
    rc = 0;
out:
    if (n) { ntlm_wipe(n); kfree(n); }
    if (msg) { memset(msg, 0, MSG_MAX); kfree(msg); }
    if (tok) { memset(tok, 0, NTLM_MSG_MAX + 1100); kfree(tok); }
    return rc;
}
