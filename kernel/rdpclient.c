#include "rdpclient.h"
#include "appwin.h"
#include "keyboard.h"
#include "task.h"
#include "timer.h"
#include "kstring.h"
#include "kheap.h"
#include "serial.h"
#include "random.h"
#include "../net/net.h"
#include "../net/tcp.h"
#include "../net/tls12.h"
#include "../net/credssp.h"
#include "../net/rdp.h"
#include "../sdk/include/banana_api.h"

/*
 * Remote Desktop client - see rdpclient.h. Section numbers: MS-RDPBCGR.
 * The window is an appwin owned by this task; the remote screen is drawn
 * into it, the keyboard (raw set-1 scancodes while the window has focus)
 * and the mouse are sent as slow-path input events.
 */

#define IN_CAP   (1u << 18)
#define OUT_CAP  (1u << 16)
#define CLIENT_CHANNEL 1007

typedef struct {
    tcp_conn_t*   tcp;
    tls12_conn_t* tls;
    rdp_params_t  p;

    uint8_t*      in;
    uint32_t      in_len;
    uint8_t*      pdu;              /* one received PDU's payload */
    uint32_t      pdu_len;
    uint16_t      pdu_channel;
    int           pdu_fastpath;
    int           mcs_choice;
    uint8_t*      out;

    uint16_t      user_channel;    /* granted by Attach User Confirm */
    uint16_t      io_channel;      /* the server's I/O channel */
    uint32_t      share_id;

    int           width, height;
    int           win;             /* appwin id */
    int           owner;           /* our pid */
    uint32_t*     fb;              /* the window pixel buffer */
    int           fb_w, fb_h;
    int           focused;
    int           active;
    int           buttons;

    char          err[160];
    volatile int  stop;
} rdpc_t;

/* ── transport ────────────────────────────────────────────────────── */

static int c_send(rdpc_t* s, const uint8_t* d, uint32_t n) {
    return tls12_write(s->tls, d, n, 30000) == (int)n ? 0 : -1;
}

static int fill(rdpc_t* s, uint32_t need, uint32_t timeout_ms) {
    uint32_t start = timer_ms();
    while (s->in_len < need) {
        uint32_t left = timeout_ms;
        if (timeout_ms) {
            uint32_t el = timer_ms() - start;
            if (el >= timeout_ms) return NET_ERR_TIMEOUT;
            left = timeout_ms - el;
        }
        int r = tls12_read(s->tls, s->in + s->in_len, IN_CAP - s->in_len, left ? left : 1);
        if (r == 0) return NET_ERR_CLOSED;
        if (r < 0) return r;
        s->in_len += (uint32_t)r;
    }
    return 0;
}
static void in_consume(rdpc_t* s, uint32_t n) {
    if (n > s->in_len) n = s->in_len;
    memmove(s->in, s->in + n, s->in_len - n);
    s->in_len -= n;
}

/* one PDU into s->pdu. TPKT: SDIN payload (with channel) or an MCS control
 * PDU (s->mcs_choice set, s->pdu = bytes from the choice byte). Fast path:
 * the update region (after the header). Returns 0 or a NET_ERR_*. */
static int recv_pdu(rdpc_t* s, uint32_t timeout_ms) {
    int r = fill(s, 1, timeout_ms);
    if (r) return r;
    s->pdu_fastpath = 0;
    s->mcs_choice = -1;
    s->pdu_channel = 0xFFFF;

    if (s->in[0] == 0x03) {
        if ((r = fill(s, 4, timeout_ms))) return r;
        uint32_t total = (uint32_t)s->in[2] << 8 | s->in[3];
        if (total < 5 || total > IN_CAP) return NET_ERR_PROTO;
        if ((r = fill(s, total, timeout_ms))) return r;
        uint32_t x = 4 + 1 + s->in[4];
        if (x > total) return NET_ERR_PROTO;
        rdp_r_t rr = { s->in, total, x, 0 };
        int choice = rr_u8(&rr) >> 2;
        s->mcs_choice = choice;
        if (choice == MCS_SDIN) {
            rr_skip(&rr, 2);                        /* initiator */
            s->pdu_channel = rr_be16(&rr);
            rr_skip(&rr, 1);
            uint32_t dl = rr_u8(&rr);
            if (dl & 0x80) dl = (dl & 0x7F) << 8 | rr_u8(&rr);
            if (rr.err || rr.off + dl > total) { in_consume(s, total); return NET_ERR_PROTO; }
            memcpy(s->pdu, s->in + rr.off, dl);
            s->pdu_len = dl;
        } else if (choice == MCS_DPUM) {
            in_consume(s, total);
            return NET_ERR_CLOSED;
        } else {
            memcpy(s->pdu, s->in + x, total - x);
            s->pdu_len = total - x;
        }
        in_consume(s, total);
        return 0;
    }

    if ((r = fill(s, 2, timeout_ms))) return r;     /* fast-path output */
    uint32_t h1 = s->in[1], total, hdrlen;
    if (h1 & 0x80) { if ((r = fill(s, 3, timeout_ms))) return r; total = (h1 & 0x7F) << 8 | s->in[2]; hdrlen = 3; }
    else { total = h1; hdrlen = 2; }
    if (total < hdrlen || total > IN_CAP) return NET_ERR_PROTO;
    if ((r = fill(s, total, timeout_ms))) return r;
    memcpy(s->pdu, s->in + hdrlen, total - hdrlen);
    s->pdu_len = total - hdrlen;
    s->pdu_fastpath = 1;
    in_consume(s, total);
    return 0;
}

/* ── X.224 + MCS writers ──────────────────────────────────────────── */

static int send_x224_data(rdpc_t* s, const uint8_t* mcs, uint32_t len) {
    uint8_t h[7];
    rdp_w_t w = { h, 0, sizeof(h), 0 };
    rw_u8(&w, 3); rw_u8(&w, 0); rw_be16(&w, (uint16_t)(7 + len));
    rw_u8(&w, 2); rw_u8(&w, 0xF0); rw_u8(&w, 0x80);
    uint8_t buf[2048];
    if (7 + len <= sizeof(buf)) { memcpy(buf, h, 7); memcpy(buf + 7, mcs, len); return c_send(s, buf, 7 + len); }
    if (c_send(s, h, 7)) return -1;
    return c_send(s, mcs, len);
}

/* Send Data Request on `channel`, carrying `data` */
static int send_sdrq(rdpc_t* s, uint16_t channel, const uint8_t* data, uint32_t len) {
    uint8_t h[8];
    rdp_w_t w = { h, 0, sizeof(h), 0 };
    rw_u8(&w, (uint8_t)(MCS_SDRQ << 2));
    rw_be16(&w, (uint16_t)(s->user_channel - MCS_USERCHANNEL_BASE));   /* initiator, base 1001 */
    rw_be16(&w, channel);
    rw_u8(&w, 0x70);
    if (len >= 128) rw_u8(&w, (uint8_t)(0x80 | (len >> 8)));
    rw_u8(&w, (uint8_t)len);
    uint8_t tp[7 + 8];
    rdp_w_t t = { tp, 0, sizeof(tp), 0 };
    rw_u8(&t, 3); rw_u8(&t, 0); rw_be16(&t, (uint16_t)(7 + w.len + len));
    rw_u8(&t, 2); rw_u8(&t, 0xF0); rw_u8(&t, 0x80);
    rw_bytes(&t, h, w.len);
    uint8_t buf[2600];
    if (t.len + len <= sizeof(buf)) { memcpy(buf, tp, t.len); memcpy(buf + t.len, data, len); return c_send(s, buf, t.len + len); }
    if (c_send(s, tp, t.len)) return -1;
    return c_send(s, data, len);
}

/* a Data PDU (share data header) on the I/O channel */
static int send_data_pdu(rdpc_t* s, uint8_t type2, const uint8_t* body, uint32_t len) {
    uint8_t* p = s->out;
    uint32_t total = 6 + 12 + len;
    rdp_w_t w = { p, 0, OUT_CAP, 0 };
    rw_le16(&w, (uint16_t)total);
    rw_le16(&w, (uint16_t)(PDUTYPE_DATA | PDU_PROTOCOL_VERSION));
    rw_le16(&w, s->user_channel);
    rw_le32(&w, s->share_id);
    rw_u8(&w, 0); rw_u8(&w, 1);                      /* pad, streamId LOW */
    rw_le16(&w, (uint16_t)total);
    rw_u8(&w, type2);
    rw_u8(&w, 0); rw_le16(&w, 0);
    rw_bytes(&w, body, len);
    if (w.err) return -1;
    return send_sdrq(s, s->io_channel, p, w.len);
}

/* ── small UTF-16LE helper ────────────────────────────────────────── */
static uint32_t utf16(const char* str, uint8_t* out, uint32_t cap) {
    uint32_t o = 0;
    for (const char* c = str; *c && o + 2 <= cap; c++) { out[o++] = (uint8_t)*c; out[o++] = 0; }
    return o;
}

/* ── connection sequence ──────────────────────────────────────────── */

static int do_negotiate(rdpc_t* s, uint32_t* selected) {
    uint8_t req[19];
    rdp_w_t w = { req, 0, sizeof(req), 0 };
    uint32_t want = PROTOCOL_SSL | (s->p.nla ? PROTOCOL_HYBRID : 0);
    rw_u8(&w, 3); rw_u8(&w, 0); rw_be16(&w, 19);
    rw_u8(&w, 14); rw_u8(&w, X224_CR); rw_be16(&w, 0); rw_be16(&w, 0); rw_u8(&w, 0);
    rw_u8(&w, RDP_NEG_REQ); rw_u8(&w, 0); rw_le16(&w, 8); rw_le32(&w, want);
    if (tcp_send(s->tcp, req, w.len, 5000) != (int)w.len) return -1;

    uint8_t cc[64];
    uint32_t have = 0, start = timer_ms();
    while (have < 4 || have < ((uint32_t)cc[2] << 8 | cc[3])) {
        if (timer_ms() - start > 10000) return -1;
        int r = tcp_recv(s->tcp, cc + have, sizeof(cc) - have, 3000);
        if (r == 0 || (r < 0 && r != NET_ERR_TIMEOUT)) return -1;
        if (r > 0) have += (uint32_t)r;
        if (have >= 4 && ((uint32_t)cc[2] << 8 | cc[3]) > sizeof(cc)) return -1;
    }
    *selected = PROTOCOL_SSL;
    for (uint32_t i = 11; i + 8 <= have; i++)
        if (cc[i] == RDP_NEG_RSP) { *selected = (uint32_t)cc[i + 4] | cc[i + 5] << 8 | cc[i + 6] << 16 | (uint32_t)cc[i + 7] << 24; break; }
        else if (cc[i] == RDP_NEG_FAILURE) { ksnprintf(s->err, sizeof(s->err), "the server refused the connection (code %u)", cc[i + 4]); return -1; }
    return 0;
}

/* MCS Connect Initial with a GCC Conference Create Request (2.2.1.3) */
static int send_connect_initial(rdpc_t* s, uint32_t selected) {
    uint8_t ud[512];
    rdp_w_t u = { ud, 0, sizeof(ud), 0 };
    /* CS_CORE (0xC001) */
    uint32_t core = u.len;
    rw_le16(&u, 0xC001); rw_le16(&u, 0);
    rw_le32(&u, 0x00080004);                         /* version RDP 5+ */
    rw_le16(&u, (uint16_t)s->width);
    rw_le16(&u, (uint16_t)s->height);
    rw_le16(&u, 0xCA01);                             /* colorDepth 8bpp (legacy) */
    rw_le16(&u, 0xAA03);                             /* SASSequence */
    rw_le32(&u, 0x409);                              /* keyboardLayout US */
    rw_le32(&u, 2600);                               /* clientBuild */
    uint8_t nm[32]; memset(nm, 0, 32); utf16("BANANA", nm, 30);
    rw_bytes(&u, nm, 32);                            /* clientName UTF-16 */
    rw_le32(&u, 4);                                  /* keyboardType */
    rw_le32(&u, 0);                                  /* keyboardSubType */
    rw_le32(&u, 12);                                 /* keyboardFunctionKey */
    for (int i = 0; i < 64; i++) rw_u8(&u, 0);       /* imeFileName */
    rw_le16(&u, 0xCA01);                             /* postBeta2ColorDepth */
    rw_le16(&u, 1);                                  /* clientProductId */
    rw_le32(&u, 0);                                  /* serialNumber */
    rw_le16(&u, 0x0018);                             /* highColorDepth: 24 */
    rw_le16(&u, 0x000F);                             /* supportedColorDepths 15/16/24/32 */
    rw_le16(&u, 0x0001 | 0x0002);                    /* earlyCaps: ERRINFO | WANT_32BPP */
    for (int i = 0; i < 64; i++) rw_u8(&u, 0);       /* clientDigProductId */
    rw_u8(&u, 0);                                    /* connectionType */
    rw_u8(&u, 0);                                    /* pad */
    rw_le32(&u, selected);                           /* serverSelectedProtocol */
    { uint16_t n = (uint16_t)(u.len - core); u.p[core + 2] = (uint8_t)n; u.p[core + 3] = (uint8_t)(n >> 8); }
    /* CS_SECURITY (0xC002): TLS, so no RDP encryption */
    rw_le16(&u, 0xC002); rw_le16(&u, 12);
    rw_le32(&u, 0); rw_le32(&u, 0);
    /* CS_NET (0xC003): no virtual channels */
    rw_le16(&u, 0xC003); rw_le16(&u, 8);
    rw_le32(&u, 0);
    if (u.err) return -1;

    /* GCC Conference Create Request wrapper */
    uint8_t gcc[600];
    rdp_w_t g = { gcc, 0, sizeof(gcc), 0 };
    static const uint8_t PRE[] = { 0x00, 0x05, 0x00, 0x14, 0x7C, 0x00, 0x01 };
    rw_bytes(&g, PRE, sizeof(PRE));
    uint32_t l1 = 6 + 2 + 4 + (u.len < 128 ? 1u : 2u) + u.len;   /* connectPDU length */
    if (l1 >= 128) { rw_u8(&g, (uint8_t)(0x80 | (l1 >> 8))); rw_u8(&g, (uint8_t)l1); }
    else rw_u8(&g, (uint8_t)l1);
    static const uint8_t CGP[] = { 0x00, 0x08, 0x00, 0x10, 0x00, 0x01, 0xC0, 0x00, 0x44, 0x75, 0x63, 0x61 }; /* ...Duca */
    rw_bytes(&g, CGP, sizeof(CGP));
    if (u.len >= 128) { rw_u8(&g, (uint8_t)(0x80 | (u.len >> 8))); rw_u8(&g, (uint8_t)u.len); }
    else rw_u8(&g, (uint8_t)u.len);
    rw_bytes(&g, ud, u.len);
    if (g.err) return -1;

    /* MCS Connect-Initial (BER) */
    uint8_t m[800];
    rdp_w_t w = { m, 0, sizeof(m), 0 };
    rw_u8(&w, 0x7F); rw_u8(&w, 0x65);                /* Connect-Initial [APPLICATION 101] */
    rw_u8(&w, 0x82); rw_be16(&w, (uint16_t)(g.len + 100));
    static const uint8_t PRE2[] = {
        0x04, 0x01, 0x01,                            /* callingDomainSelector */
        0x04, 0x01, 0x01,                            /* calledDomainSelector */
        0x01, 0x01, 0xFF,                            /* upwardFlag TRUE */
        /* targetParameters */
        0x30, 0x19, 0x02, 0x01, 0x22, 0x02, 0x01, 0x02, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01,
        0x02, 0x01, 0x00, 0x02, 0x01, 0x01, 0x02, 0x02, 0xFF, 0xFF, 0x02, 0x01, 0x02,
        /* minimumParameters */
        0x30, 0x19, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01,
        0x02, 0x01, 0x00, 0x02, 0x01, 0x01, 0x02, 0x02, 0x04, 0x20, 0x02, 0x01, 0x02,
        /* maximumParameters */
        0x30, 0x1C, 0x02, 0x02, 0xFF, 0xFF, 0x02, 0x02, 0xFC, 0x17, 0x02, 0x02, 0xFF, 0xFF,
        0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01, 0x02, 0x02, 0xFF, 0xFF, 0x02, 0x01, 0x02,
    };
    rw_bytes(&w, PRE2, sizeof(PRE2));
    rw_u8(&w, 0x04); rw_u8(&w, 0x82); rw_be16(&w, (uint16_t)g.len);   /* userData OCTET STRING */
    rw_bytes(&w, gcc, g.len);
    if (w.err) return -1;
    return send_x224_data(s, m, w.len);
}

/* read the Connect Response, pick out the I/O channel (and server vcs) */
static int parse_connect_response(rdpc_t* s) {
    const uint8_t* p = s->pdu;
    uint32_t n = s->pdu_len;
    uint32_t i = 0;
    while (i + 4 < n && !(p[i] == 'M' && p[i + 1] == 'c' && p[i + 2] == 'D' && p[i + 3] == 'n')) i++;
    if (i + 6 >= n) { s->io_channel = MCS_GLOBAL_CHANNEL; return 0; }
    i += 4;
    i += (p[i] & 0x80) ? 2 : 1;                      /* PER length of user data */
    s->io_channel = MCS_GLOBAL_CHANNEL;
    while (i + 4 <= n) {
        uint16_t type = (uint16_t)(p[i] | p[i + 1] << 8);
        uint16_t len = (uint16_t)(p[i + 2] | p[i + 3] << 8);
        if (len < 4 || i + len > n) break;
        if (type == 0x0C03 && len >= 8)              /* SC_NET: MCSChannelId */
            s->io_channel = (uint16_t)(p[i + 4] | p[i + 5] << 8);
        i += len;
    }
    return 0;
}

static int expect(rdpc_t* s, int fastpath_ok, uint32_t timeout_ms) {
    int r = recv_pdu(s, timeout_ms);
    if (r) { if (!s->err[0]) ksnprintf(s->err, sizeof(s->err), "lost the connection (%s)", net_strerror(r)); return r; }
    if (s->pdu_fastpath && !fastpath_ok) return NET_ERR_PROTO;
    return 0;
}

/* Client Info PDU (2.2.1.11): credentials + options */
static int send_client_info(rdpc_t* s) {
    uint8_t* p = s->out;
    rdp_w_t w = { p, 0, OUT_CAP, 0 };
    rw_le16(&w, SEC_INFO_PKT);
    rw_le16(&w, 0);
    uint8_t dom[128], usr[128], pw[256];
    uint32_t dl = utf16(s->p.domain, dom, sizeof(dom));
    uint32_t ul = utf16(s->p.user, usr, sizeof(usr));
    uint32_t pl = utf16(s->p.password, pw, sizeof(pw));
    uint32_t flags = 0x00000001 | 0x00000010 | 0x00000020 | 0x00000040 | 0x00000002;
                     /* INFO_MOUSE | INFO_UNICODE | INFO_MAXIMIZESHELL | INFO_LOGONNOTIFY | INFO_DISABLECTRLALTDEL */
    if (s->p.password[0]) flags |= 0x00000008;                           /* INFO_AUTOLOGON */
    rw_le32(&w, 0);                                  /* CodePage */
    rw_le32(&w, flags);
    rw_le16(&w, (uint16_t)dl);
    rw_le16(&w, (uint16_t)ul);
    rw_le16(&w, (uint16_t)pl);
    rw_le16(&w, 0);                                  /* cbAlternateShell */
    rw_le16(&w, 0);                                  /* cbWorkingDir */
    rw_bytes(&w, dom, dl); rw_le16(&w, 0);
    rw_bytes(&w, usr, ul); rw_le16(&w, 0);
    rw_bytes(&w, pw, pl); rw_le16(&w, 0);
    rw_le16(&w, 0);                                  /* alternateShell */
    rw_le16(&w, 0);                                  /* workingDir */
    /* extendedInfo (RDP5) */
    rw_le16(&w, 2);                                  /* clientAddressFamily AF_INET */
    rw_le16(&w, 2); rw_le16(&w, 0);                  /* cbClientAddress + "" */
    rw_le16(&w, 2); rw_le16(&w, 0);                  /* cbClientDir + "" */
    for (int i = 0; i < 172; i++) rw_u8(&w, 0);      /* clientTimeZone */
    rw_le32(&w, 0);                                  /* clientSessionId */
    rw_le32(&w, 0);                                  /* performanceFlags */
    rw_le16(&w, 0);                                  /* cbAutoReconnectCookie */
    memset(pw, 0, sizeof(pw));
    if (w.err) return -1;
    return send_sdrq(s, s->io_channel, p, w.len);
}

/* ── capability sets for Confirm Active ───────────────────────────── */

static void cend(rdp_w_t* w, uint32_t m) { uint16_t n = (uint16_t)(w->len - m); w->p[m + 2] = (uint8_t)n; w->p[m + 3] = (uint8_t)(n >> 8); }

static int send_confirm_active(rdpc_t* s) {
    uint8_t* p = s->out;
    rdp_w_t w = { p, 0, OUT_CAP, 0 };
    rw_le16(&w, (uint16_t)0);                        /* shareControl totalLength (patched) */
    rw_le16(&w, PDUTYPE_CONFIRMACTIVE | PDU_PROTOCOL_VERSION);
    rw_le16(&w, s->user_channel);
    rw_le32(&w, s->share_id);
    rw_le16(&w, 0x03EA);                             /* originatorId */
    rw_le16(&w, 4);                                  /* lengthSourceDescriptor */
    uint32_t caplen_at = w.len;
    rw_le16(&w, 0);                                  /* lengthCombinedCapabilities */
    rw_bytes(&w, "RDP\0", 4);
    uint32_t ncap_at = w.len;
    rw_le16(&w, 0); rw_le16(&w, 0);                  /* numberCapabilities, pad */
    uint32_t caps = w.len;
    int n = 0;
    /* General */
    { uint32_t m = w.len; rw_le16(&w, CAPSET_GENERAL); rw_le16(&w, 0);
      rw_le16(&w, 1); rw_le16(&w, 3); rw_le16(&w, 0x0200); rw_le16(&w, 0);
      rw_le16(&w, 0); rw_le16(&w, 0x0001 | 0x0004 | 0x0008 | 0x0400);   /* FASTPATH | LONGCRED | AUTORECONNECT | NO_HDR */
      rw_le16(&w, 0); rw_le16(&w, 0); rw_le16(&w, 0); rw_u8(&w, 0); rw_u8(&w, 0);
      cend(&w, m); n++; }
    /* Bitmap: 32bpp, compression supported (we decode it) */
    { uint32_t m = w.len; rw_le16(&w, CAPSET_BITMAP); rw_le16(&w, 0);
      rw_le16(&w, 32); rw_le16(&w, 1); rw_le16(&w, 1); rw_le16(&w, 1);
      rw_le16(&w, (uint16_t)s->width); rw_le16(&w, (uint16_t)s->height);
      rw_le16(&w, 0); rw_le16(&w, 1); rw_le16(&w, 1);                   /* desktopResize, compression */
      rw_u8(&w, 0); rw_u8(&w, 0); rw_le16(&w, 1); rw_le16(&w, 0);
      cend(&w, m); n++; }
    /* Order: no orders (bitmaps only) */
    { uint32_t m = w.len; rw_le16(&w, CAPSET_ORDER); rw_le16(&w, 0);
      for (int i = 0; i < 16; i++) rw_u8(&w, 0);
      rw_le32(&w, 0); rw_le16(&w, 1); rw_le16(&w, 20); rw_le16(&w, 0); rw_le16(&w, 1); rw_le16(&w, 0);
      rw_le16(&w, 0x0022);
      for (int i = 0; i < 32; i++) rw_u8(&w, 0);
      rw_le16(&w, 0); rw_le16(&w, 0); rw_le32(&w, 0); rw_le32(&w, 230400);
      rw_le16(&w, 0); rw_le16(&w, 0); rw_le16(&w, 0); rw_le16(&w, 0);
      cend(&w, m); n++; }
    /* Bitmap cache (rev1): all zero = cache nothing */
    { uint32_t m = w.len; rw_le16(&w, 4); rw_le16(&w, 0);
      for (int i = 0; i < 36; i++) rw_u8(&w, 0);
      cend(&w, m); n++; }
    /* Color table cache */
    { uint32_t m = w.len; rw_le16(&w, CAPSET_COLORCACHE); rw_le16(&w, 0); rw_le16(&w, 6); rw_le16(&w, 0); cend(&w, m); n++; }
    /* Pointer */
    { uint32_t m = w.len; rw_le16(&w, CAPSET_POINTER); rw_le16(&w, 0); rw_le16(&w, 1); rw_le16(&w, 20); rw_le16(&w, 20); cend(&w, m); n++; }
    /* Input */
    { uint32_t m = w.len; rw_le16(&w, CAPSET_INPUT); rw_le16(&w, 0);
      rw_le16(&w, 0x0001 | 0x0004 | 0x0008 | 0x0020);                  /* SCANCODES | MOUSEX | FASTPATH | FASTPATH2 */
      rw_le16(&w, 0); rw_le32(&w, 0x409); rw_le32(&w, 4); rw_le32(&w, 0); rw_le32(&w, 12);
      for (int i = 0; i < 64; i++) rw_u8(&w, 0);
      cend(&w, m); n++; }
    /* Share */
    { uint32_t m = w.len; rw_le16(&w, CAPSET_SHARE); rw_le16(&w, 0); rw_le16(&w, 0); rw_le16(&w, 0); cend(&w, m); n++; }
    uint16_t combined = (uint16_t)(w.len - caps + 4);
    w.p[caplen_at] = (uint8_t)combined; w.p[caplen_at + 1] = (uint8_t)(combined >> 8);
    w.p[ncap_at] = (uint8_t)n; w.p[ncap_at + 1] = (uint8_t)(n >> 8);
    uint16_t total = (uint16_t)w.len;
    w.p[0] = (uint8_t)total; w.p[1] = (uint8_t)(total >> 8);
    if (w.err) return -1;
    return send_sdrq(s, s->io_channel, p, w.len);
}

static int send_synchronize(rdpc_t* s) {
    uint8_t b[4]; rdp_w_t w = { b, 0, 4, 0 };
    rw_le16(&w, 1); rw_le16(&w, 1002);
    return send_data_pdu(s, PDUTYPE2_SYNCHRONIZE, b, 4);
}
static int send_control(rdpc_t* s, uint16_t action) {
    uint8_t b[8]; rdp_w_t w = { b, 0, 8, 0 };
    rw_le16(&w, action); rw_le16(&w, 0); rw_le32(&w, 0);
    return send_data_pdu(s, PDUTYPE2_CONTROL, b, 8);
}
static int send_font_list(rdpc_t* s) {
    uint8_t b[8]; rdp_w_t w = { b, 0, 8, 0 };
    rw_le16(&w, 0); rw_le16(&w, 0); rw_le16(&w, 3); rw_le16(&w, 0x0032);
    return send_data_pdu(s, PDUTYPE2_FONTLIST, b, 8);
}

/* ── input ────────────────────────────────────────────────────────── */

static int send_input(rdpc_t* s, uint16_t type, uint16_t a, uint16_t b, uint16_t c) {
    uint8_t body[16];
    rdp_w_t w = { body, 0, sizeof(body), 0 };
    rw_le16(&w, 1);                                  /* numberEvents */
    rw_le16(&w, 0);                                  /* pad */
    rw_le32(&w, 0);                                  /* eventTime */
    rw_le16(&w, type);
    rw_le16(&w, a); rw_le16(&w, b); rw_le16(&w, c);
    return send_data_pdu(s, PDUTYPE2_INPUT, body, w.len);
}
static void send_scancode(rdpc_t* s, uint8_t code, int down, int e0) {
    uint16_t fl = (down ? 0 : KBDFLAGS_RELEASE) | (e0 ? KBDFLAGS_EXTENDED : 0);
    send_input(s, INPUT_EVENT_SCANCODE, fl, code, 0);
}
static void send_mouse(rdpc_t* s, uint16_t flags, int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    send_input(s, INPUT_EVENT_MOUSE, flags, (uint16_t)x, (uint16_t)y);
}

/* ── bitmap decoding ──────────────────────────────────────────────── */

static uint32_t from16(uint16_t v) {       /* RGB565 */
    uint32_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}
static uint32_t from15(uint16_t v) {       /* RGB555 */
    uint32_t r = (v >> 10) & 0x1F, g = (v >> 5) & 0x1F, b = v & 0x1F;
    return (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
}

/* draw a decoded tile (top-down ARGB in `tile`) into the window */
static void blit(rdpc_t* s, int dx, int dy, int w, int h, const uint32_t* tile) {
    for (int y = 0; y < h; y++) {
        int py = dy + y;
        if (py < 0 || py >= s->fb_h) continue;
        uint32_t* drow = s->fb + (uint32_t)py * s->fb_w;
        const uint32_t* srow = tile + (uint32_t)y * w;
        for (int x = 0; x < w; x++) {
            int px = dx + x;
            if (px >= 0 && px < s->fb_w) drow[px] = srow[x];
        }
    }
}

int rdp_rle_decompress(const uint8_t* in, uint32_t in_len, int bpp, int w, int h, uint32_t* out);   /* rdpclient_rle.c */
int rdp_planar_decompress(const uint8_t* in, uint32_t in_len, int w, int h, uint32_t* out);

/* one TS_BITMAP_DATA rectangle */
static void draw_bitmap(rdpc_t* s, const uint8_t* b, uint32_t n) {
    rdp_r_t r = { b, n, 0, 0 };
    int left = rr_le16(&r), top = rr_le16(&r), right = rr_le16(&r), bottom = rr_le16(&r);
    int w = rr_le16(&r), h = rr_le16(&r);
    int bpp = rr_le16(&r);
    uint16_t flags = rr_le16(&r);
    uint32_t blen = rr_le16(&r);
    (void)right; (void)bottom;
    if (r.err || w <= 0 || h <= 0 || w > 4096 || h > 2048) return;
    const uint8_t* data = b + r.off;
    if (r.off + blen > n) return;
    if ((flags & BITMAP_COMPRESSION) && !(flags & NO_BITMAP_COMPRESSION_HDR) && blen >= 8) {
        data += 8; blen -= 8;                        /* skip the bitmapComprHdr */
    }
    uint32_t* tile = (uint32_t*)kmalloc((uint32_t)w * h * 4);
    if (!tile) return;
    int ok = 0;
    if (!(flags & BITMAP_COMPRESSION)) {
        /* uncompressed, rows bottom-up */
        const uint8_t* p = data;
        for (int y = 0; y < h; y++) {
            uint32_t* o = tile + (uint32_t)(h - 1 - y) * w;
            if (bpp == 32) { for (int x = 0; x < w; x++) { o[x] = (uint32_t)p[0] | p[1] << 8 | (uint32_t)p[2] << 16; p += 4; } }
            else if (bpp == 24) { for (int x = 0; x < w; x++) { o[x] = (uint32_t)p[0] | p[1] << 8 | (uint32_t)p[2] << 16; p += 3; } }
            else if (bpp == 16) { for (int x = 0; x < w; x++) { o[x] = from16((uint16_t)(p[0] | p[1] << 8)); p += 2; } }
            else if (bpp == 15) { for (int x = 0; x < w; x++) { o[x] = from15((uint16_t)(p[0] | p[1] << 8)); p += 2; } }
            else { kfree(tile); return; }
        }
        ok = 1;
    } else if (bpp == 32) {
        ok = rdp_planar_decompress(data, blen, w, h, tile) == 0;
    } else {
        ok = rdp_rle_decompress(data, blen, bpp, w, h, tile) == 0;
    }
#ifdef RDPCLIENT_HOST_TEST
    if (!ok) {
        extern void rdpc_dump_bitmap(const uint8_t*, uint32_t, int, int, int, uint16_t);
        rdpc_dump_bitmap(data, blen, bpp, w, h, flags);
    }
#endif
    if (ok) blit(s, left, top, w, h, tile);
    else klog("rdp: undecoded bitmap bpp=%d flags=%04x len=%u %dx%d\n", bpp, flags, blen, w, h);
    kfree(tile);
}

/* a fast-path or slow-path bitmap UPDATE (list of rectangles) */
static void handle_bitmap_update(rdpc_t* s, rdp_r_t* r) {
    uint16_t num = rr_le16(r);
    for (uint16_t i = 0; i < num && !r->err; i++) {
        /* each TS_BITMAP_DATA: 18-byte header then bitmapLength bytes */
        if (r->off + 18 > r->len) break;
        uint32_t blen = (uint32_t)(r->p[r->off + 16] | r->p[r->off + 17] << 8);
        if (r->off + 18 + blen > r->len) break;
        draw_bitmap(s, r->p + r->off, 18 + blen);
        r->off += 18 + blen;
    }
}

/* fast-path output: one or more updates with fragment reassembly */
static void handle_fastpath_output(rdpc_t* s, uint8_t** frag, uint32_t* frag_len, uint32_t* frag_cap) {
    rdp_r_t r = { s->pdu, s->pdu_len, 0, 0 };
    while (r.off < r.len && !r.err) {
        uint8_t uh = rr_u8(&r);
        int code = uh & 0x0F;
        int fragm = (uh >> 4) & 0x03;
        int compr = (uh >> 6) & 0x03;
        if (compr & 0x02) rr_u8(&r);                 /* compressionFlags */
        uint16_t size = rr_le16(&r);
        if (r.off + size > r.len) break;
        const uint8_t* data = r.p + r.off;
        r.off += size;
        if (fragm == FASTPATH_FRAGMENT_SINGLE) {
            rdp_r_t u = { data, size, 0, 0 };
            if (code == FASTPATH_UPDATETYPE_BITMAP) { rr_le16(&u); handle_bitmap_update(s, &u); }
        } else {
            /* reassemble FIRST..NEXT..LAST */
            if (fragm == FASTPATH_FRAGMENT_FIRST) *frag_len = 0;
            if (*frag_len + size <= *frag_cap) { memcpy(*frag + *frag_len, data, size); *frag_len += size; }
            if (fragm == FASTPATH_FRAGMENT_LAST) {
                rdp_r_t u = { *frag, *frag_len, 0, 0 };
                if (code == FASTPATH_UPDATETYPE_BITMAP) { rr_le16(&u); handle_bitmap_update(s, &u); }
                *frag_len = 0;
            }
        }
    }
}

/* find a share control header, allowing an optional 4-byte basic security
 * header in front (TLS sessions usually omit it, but some servers send it).
 * Returns the PDU type (low nibble) and the header offset, or -1. */
static int share_type(const uint8_t* p, uint32_t n, uint32_t* off) {
    for (uint32_t base = 0; base <= 4; base += 4) {
        if (base + 6 > n) break;
        uint16_t pt = (uint16_t)(p[base + 2] | p[base + 3] << 8);
        int t = pt & 0x0F;
        if ((pt & PDU_PROTOCOL_VERSION) && (t == PDUTYPE_DEMANDACTIVE || t == PDUTYPE_CONFIRMACTIVE ||
                                            t == PDUTYPE_DEACTIVATEALL || t == PDUTYPE_DATA)) { *off = base; return t; }
    }
    return -1;
}

/* a slow-path share PDU from the server (Update, Deactivate All, ...) */
/* returns 1 if the session should re-activate (Deactivate All) */
static int handle_server_pdu(rdpc_t* s) {
    uint32_t off;
    int type = share_type(s->pdu, s->pdu_len, &off);
    if (type < 0) return 0;                          /* a license or other PDU */
    if (type == PDUTYPE_DEACTIVATEALL) return 1;
    rdp_r_t r = { s->pdu, s->pdu_len, off + 6, 0 };  /* past totalLength/pduType/source */
    if (type != PDUTYPE_DATA) return 0;
    rr_le32(&r); rr_u8(&r); rr_u8(&r); rr_le16(&r);
    uint8_t type2 = rr_u8(&r);
    rr_u8(&r); rr_le16(&r);
    if (r.err) return 0;
    if (type2 == PDUTYPE2_UPDATE) {
        uint16_t ut = rr_le16(&r);
        if (ut == UPDATETYPE_BITMAP) handle_bitmap_update(s, &r);
    }
    return 0;
}

/* ── the session ──────────────────────────────────────────────────── */

static int connect_sequence(rdpc_t* s) {
    uint32_t selected;
    if (do_negotiate(s, &selected) != 0) { if (!s->err[0]) ksnprintf(s->err, sizeof(s->err), "X.224 negotiation failed"); return -1; }

    int err;
    s->tls = tls12_connect(s->tcp, s->p.host, 15000, &err, s->err, sizeof(s->err));
    if (!s->tls) return -1;

    if (selected & PROTOCOL_HYBRID) {
        if (credssp_login(s->tls, s->p.user, s->p.domain, s->p.password, s->p.host, 15000, s->err, sizeof(s->err)) != 0)
            return -1;
    }

    if (send_connect_initial(s, selected) != 0) { ksnprintf(s->err, sizeof(s->err), "could not send MCS Connect"); return -1; }
    if (expect(s, 0, 15000) != 0) return -1;
    parse_connect_response(s);

    /* Erect Domain Request */
    { uint8_t b[6]; rdp_w_t w = { b, 0, 6, 0 }; rw_u8(&w, MCS_EDRQ << 2); rw_u8(&w, 1); rw_u8(&w, 0); rw_u8(&w, 1); rw_u8(&w, 0);
      if (send_x224_data(s, b, w.len)) return -1; }
    /* Attach User Request -> Confirm */
    { uint8_t b[1] = { MCS_AURQ << 2 }; if (send_x224_data(s, b, 1)) return -1; }
    if (expect(s, 0, 10000) != 0) return -1;
    if (s->mcs_choice != MCS_AUCF || s->pdu_len < 4) { ksnprintf(s->err, sizeof(s->err), "no Attach User Confirm"); return -1; }
    s->user_channel = (uint16_t)(MCS_USERCHANNEL_BASE + (s->pdu[2] << 8 | s->pdu[3]));

    /* join the user channel and the I/O channel */
    uint16_t joins[2] = { s->user_channel, s->io_channel };
    for (int i = 0; i < 2; i++) {
        uint8_t b[5]; rdp_w_t w = { b, 0, 5, 0 };
        /* initiator is PER integer16 with base 1001 (userId - 1001) */
        rw_u8(&w, MCS_CJRQ << 2); rw_be16(&w, (uint16_t)(s->user_channel - MCS_USERCHANNEL_BASE)); rw_be16(&w, joins[i]);
        if (send_x224_data(s, b, w.len)) return -1;
        if (expect(s, 0, 10000) != 0) return -1;
        if (s->mcs_choice != MCS_CJCF) { ksnprintf(s->err, sizeof(s->err), "channel join refused"); return -1; }
    }

    if (send_client_info(s) != 0) { ksnprintf(s->err, sizeof(s->err), "could not send the login"); return -1; }

    /* the server sends a License Error PDU (valid client), then the Demand
     * Active; skip licensing (and any other) PDUs until it arrives */
    for (int guard = 0; ; guard++) {
        if (guard >= 20) { ksnprintf(s->err, sizeof(s->err), "no Demand Active PDU"); return -1; }
        if (expect(s, 1, 15000) != 0) return -1;
        if (s->pdu_fastpath) continue;
        uint32_t off;
        int t = share_type(s->pdu, s->pdu_len, &off);
        if (t == PDUTYPE_DEMANDACTIVE) { s->share_id = (uint32_t)(s->pdu[off + 6] | s->pdu[off + 7] << 8 | s->pdu[off + 8] << 16 | (uint32_t)s->pdu[off + 9] << 24); break; }
        /* a license PDU or anything else: ignore and keep waiting */
    }

    if (send_confirm_active(s) != 0) { ksnprintf(s->err, sizeof(s->err), "could not send Confirm Active"); return -1; }
    send_synchronize(s);
    send_control(s, CTRLACTION_COOPERATE);
    send_control(s, CTRLACTION_REQUEST_CONTROL);
    send_font_list(s);
    s->active = 1;
    return 0;
}

/* pull input from the window (mouse) and the keyboard (raw) and send it */
static void pump_input(rdpc_t* s) {
    banana_event_t ev;
    while (appwin_event(s->win, s->owner, &ev)) {
        if (ev.type == BANANA_EV_CLOSE) { s->stop = 1; return; }
        else if (ev.type == BANANA_EV_FOCUS) { s->focused = ev.x; keyboard_capture_raw(ev.x); }
        else if (ev.type == BANANA_EV_MOUSE_MOVE) send_mouse(s, PTRFLAGS_MOVE, ev.x, ev.y);
        else if (ev.type == BANANA_EV_MOUSE_DOWN) {
            uint16_t f = ev.button == 2 ? PTRFLAGS_BUTTON2 : ev.button == 3 ? PTRFLAGS_BUTTON3 : PTRFLAGS_BUTTON1;
            send_mouse(s, (uint16_t)(PTRFLAGS_DOWN | f), ev.x, ev.y);
        } else if (ev.type == BANANA_EV_MOUSE_UP) {
            uint16_t f = ev.button == 2 ? PTRFLAGS_BUTTON2 : ev.button == 3 ? PTRFLAGS_BUTTON3 : PTRFLAGS_BUTTON1;
            send_mouse(s, f, ev.x, ev.y);
        } else if (ev.type == BANANA_EV_WHEEL) {
            uint16_t f = PTRFLAGS_WHEEL | (ev.y < 0 ? (PTRFLAGS_WHEEL_NEGATIVE | 0x0088) : 0x0078);
            send_mouse(s, f, 0, 0);
        }
    }
    if (s->focused) {
        uint8_t code; int down, e0;
        while (keyboard_poll_raw(&code, &down, &e0)) send_scancode(s, code, down, e0);
    }
}

static void session(rdpc_t* s) {
    if (connect_sequence(s) != 0) {
        klog("rdp: %s\n", s->err[0] ? s->err : "connection failed");
        return;
    }
    klog("rdp: connected to %s (%dx%d)\n", s->p.host, s->width, s->height);
    appwin_set_title(s->win, s->owner, s->p.host);

    uint32_t frag_cap = 4u * 1024 * 1024, frag_len = 0;
    uint8_t* frag = (uint8_t*)kmalloc(frag_cap);
    uint32_t last_draw = 0;
    while (!s->stop) {
        int busy = 0;
        int r = recv_pdu(s, 0);
        if (r == 0) {
            if (s->pdu_fastpath) handle_fastpath_output(s, &frag, &frag_len, &frag_cap);
            else if (s->mcs_choice == MCS_SDIN || s->mcs_choice < 0) {
                if (handle_server_pdu(s)) {          /* Deactivate All: re-run caps */
                    s->active = 0;
                    /* wait for the next Demand Active and reconfirm */
                    for (int g = 0; g < 20 && !s->stop; g++) {
                        if (recv_pdu(s, 10000)) { s->stop = 1; break; }
                        if (s->pdu_fastpath) continue;
                        uint32_t off;
                        if (share_type(s->pdu, s->pdu_len, &off) == PDUTYPE_DEMANDACTIVE) {
                            s->share_id = (uint32_t)(s->pdu[off + 6] | s->pdu[off + 7] << 8 | s->pdu[off + 8] << 16 | (uint32_t)s->pdu[off + 9] << 24);
                            send_confirm_active(s); send_synchronize(s); send_control(s, CTRLACTION_COOPERATE);
                            send_control(s, CTRLACTION_REQUEST_CONTROL); send_font_list(s); s->active = 1; break;
                        }
                    }
                }
            }
            busy = 1;
        } else if (r != NET_ERR_TIMEOUT) {
            klog("rdp: disconnected (%s)\n", net_strerror(r));
            break;
        }
        pump_input(s);
        uint32_t now = timer_ms();
        if (busy && now - last_draw >= 30) { appwin_update(s->win, s->owner); last_draw = now; }
        if (!busy) { appwin_update(s->win, s->owner); net_wait(5); }
        if (appwin_kill_requested(s->owner)) break;
    }
    kfree(frag);
    keyboard_capture_raw(0);
}

/* ── task entry + launch ──────────────────────────────────────────── */

static rdp_params_t g_pending[4];
static volatile int g_pq_head, g_pq_tail;

static void client_task(void) {
    int slot = g_pq_tail % 4;
    g_pq_tail++;
    rdpc_t* s = (rdpc_t*)kzalloc(sizeof(rdpc_t));
    if (!s) return;
    s->p = g_pending[slot];
    memset(&g_pending[slot], 0, sizeof(rdp_params_t));
    s->owner = task_current_pid();
    s->in = (uint8_t*)kmalloc(IN_CAP);
    s->out = (uint8_t*)kmalloc(OUT_CAP);
    s->pdu = (uint8_t*)kmalloc(IN_CAP);
    s->width = 1024; s->height = 768;
    s->win = s->in && s->out && s->pdu ? appwin_open(s->owner, "Remote Desktop", s->width, s->height) : -1;
    if (s->win >= 0) {
        appwin_size(s->win, s->owner, &s->fb_w, &s->fb_h);
        s->width = s->fb_w; s->height = s->fb_h;
        s->fb = appwin_pixels(s->win, s->owner);
        if (s->fb) for (int i = 0; i < s->fb_w * s->fb_h; i++) s->fb[i] = 0x00101418;
        appwin_update(s->win, s->owner);
        int err;
        ip4_t ip;
        if (!str_to_ip4(s->p.host, &ip) && dns_resolve(s->p.host, &ip, 8000) != 0) {
            klog("rdp: cannot resolve %s\n", s->p.host);
            ip = 0;
        }
        s->tcp = ip ? tcp_connect(ip, s->p.port, 10000, &err) : NULL;
        if (s->tcp) session(s);
        else klog("rdp: cannot reach %s:%u\n", s->p.host, s->p.port);
        if (s->tls) tls12_close(s->tls);
        if (s->tcp) tcp_close(s->tcp);
        appwin_close(s->win, s->owner);
    }
    kfree(s->in); kfree(s->out); kfree(s->pdu);
    memset(s, 0, sizeof(*s));
    kfree(s);
}

#ifdef RDPCLIENT_HOST_TEST
uint32_t* rdpc_test_fb; int rdpc_test_w, rdpc_test_h, rdpc_test_ok;
int rdpclient_host_run(const char* host, int port, const char* user, const char* pass, const char* dom, int nla) {
    rdpc_t* s = (rdpc_t*)kzalloc(sizeof(rdpc_t));
    kstrlcpy(s->p.host, host, sizeof(s->p.host));
    s->p.port = (uint16_t)port;
    kstrlcpy(s->p.user, user, sizeof(s->p.user));
    kstrlcpy(s->p.password, pass, sizeof(s->p.password));
    kstrlcpy(s->p.domain, dom, sizeof(s->p.domain));
    s->p.nla = nla;
    s->owner = 1;
    s->in = (uint8_t*)kmalloc(IN_CAP); s->out = (uint8_t*)kmalloc(OUT_CAP); s->pdu = (uint8_t*)kmalloc(IN_CAP);
    s->width = 1024; s->height = 768;
    s->win = appwin_open(s->owner, "Remote Desktop", s->width, s->height);
    appwin_size(s->win, s->owner, &s->fb_w, &s->fb_h);
    s->width = s->fb_w; s->height = s->fb_h;
    s->fb = appwin_pixels(s->win, s->owner);
    rdpc_test_fb = s->fb; rdpc_test_w = s->fb_w; rdpc_test_h = s->fb_h;
    int err;
    ip4_t ip = 0;
    str_to_ip4(host, &ip);
    s->tcp = tcp_connect(ip, s->p.port, 10000, &err);
    if (s->tcp) session(s);
    else klog("rdp: cannot reach %s\n", host);
    rdpc_test_ok = s->active;
    if (s->tls) tls12_close(s->tls);
    if (s->tcp) tcp_close(s->tcp);
    return s->active ? 0 : -1;
}
#endif

int rdpclient_launch(const rdp_params_t* p) {
    int slot = g_pq_head % 4;
    if (g_pq_head - g_pq_tail >= 4) return -1;
    g_pending[slot] = *p;
    if (!g_pending[slot].port) g_pending[slot].port = 3389;
    g_pq_head++;
    if (task_create("rdp", client_task) < 0) { g_pq_head--; return -1; }
    return 0;
}
