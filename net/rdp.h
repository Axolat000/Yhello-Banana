/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RDP_H
#define RDP_H

#include "types.h"

/*
 * Shared Remote Desktop Protocol definitions (MS-RDPBCGR) for the server
 * (net/rdpd.c) and the client (kernel/rdpclient.c). Only what Banana OS
 * uses: TLS as the transport (so no standard RDP encryption), bitmap
 * updates (no drawing orders), keyboard and mouse input.
 */

/* X.224 (section 2.2.1) */
#define X224_CR          0xE0           /* Connection Request */
#define X224_CC          0xD0           /* Connection Confirm */
#define X224_DT          0xF0           /* Data */

/* RDP negotiation (2.2.1.1.1 / 2.2.1.2.1) */
#define RDP_NEG_REQ      0x01
#define RDP_NEG_RSP      0x02
#define RDP_NEG_FAILURE  0x03
#define PROTOCOL_RDP     0x00000000u
#define PROTOCOL_SSL     0x00000001u
#define PROTOCOL_HYBRID  0x00000002u
#define PROTOCOL_HYBRID_EX 0x00000008u
/* negotiation failure codes (2.2.1.2.2) */
#define SSL_REQUIRED_BY_SERVER      0x00000001u
#define SSL_NOT_ALLOWED_BY_SERVER   0x00000002u
#define HYBRID_REQUIRED_BY_SERVER   0x00000005u

/* MCS (T.125) PDU choices, in the top 6 bits of the first byte */
#define MCS_EDRQ   1     /* Erect Domain Request */
#define MCS_AURQ   10    /* Attach User Request */
#define MCS_AUCF   11    /* Attach User Confirm */
#define MCS_CJRQ   14    /* Channel Join Request */
#define MCS_CJCF   15    /* Channel Join Confirm */
#define MCS_SDRQ   25    /* Send Data Request */
#define MCS_SDIN   26    /* Send Data Indication */
#define MCS_DPUM   8     /* Disconnect Provider Ultimatum */

/* well-known MCS channels */
#define MCS_USERCHANNEL_BASE 1001
#define MCS_GLOBAL_CHANNEL   1003

/* Security header flags (2.2.8.1.1.2.1) */
#define SEC_EXCHANGE_PKT  0x0001
#define SEC_INFO_PKT      0x0040
#define SEC_LICENSE_PKT   0x0080

/* Share control header pduType (low 4 bits; 2.2.8.1.1.1.1) */
#define PDUTYPE_DEMANDACTIVE 1
#define PDUTYPE_CONFIRMACTIVE 3
#define PDUTYPE_DEACTIVATEALL 6
#define PDUTYPE_DATA         7
#define PDU_PROTOCOL_VERSION 0x0010

/* Share data header pduType2 (2.2.8.1.1.1.2) */
#define PDUTYPE2_UPDATE          2
#define PDUTYPE2_CONTROL         20
#define PDUTYPE2_POINTER         27
#define PDUTYPE2_INPUT           28
#define PDUTYPE2_SYNCHRONIZE     31
#define PDUTYPE2_FONTLIST        39
#define PDUTYPE2_FONTMAP         40
#define PDUTYPE2_SET_ERROR_INFO  47

/* Control actions (2.2.1.15.1) */
#define CTRLACTION_REQUEST_CONTROL 1
#define CTRLACTION_GRANTED_CONTROL 2
#define CTRLACTION_DETACH          3
#define CTRLACTION_COOPERATE       4

/* Updates (slow path, 2.2.9.1.1.3) and the fast-path update codes */
#define UPDATETYPE_ORDERS    0
#define UPDATETYPE_BITMAP    1
#define UPDATETYPE_PALETTE   2
#define UPDATETYPE_SYNC      3
#define FASTPATH_UPDATETYPE_BITMAP  1
#define FASTPATH_UPDATETYPE_PALETTE 2
#define FASTPATH_UPDATETYPE_SYNC    3
#define FASTPATH_UPDATETYPE_PTR_NULL   5
#define FASTPATH_UPDATETYPE_PTR_DEFAULT 6
#define FASTPATH_FRAGMENT_SINGLE 0
#define FASTPATH_FRAGMENT_LAST   2
#define FASTPATH_FRAGMENT_FIRST  1
#define FASTPATH_FRAGMENT_NEXT   3

/* Bitmap data flags (2.2.9.1.1.3.1.2.2) */
#define BITMAP_COMPRESSION   0x0001
#define NO_BITMAP_COMPRESSION_HDR 0x0400

/* Input event types (2.2.8.1.1.3.1.1) */
#define INPUT_EVENT_SYNC     0x0000
#define INPUT_EVENT_SCANCODE 0x0004
#define INPUT_EVENT_UNICODE  0x0005
#define INPUT_EVENT_MOUSE    0x8001
#define INPUT_EVENT_MOUSEX   0x8002
/* keyboard flags */
#define KBDFLAGS_EXTENDED 0x0100
#define KBDFLAGS_DOWN     0x4000
#define KBDFLAGS_RELEASE  0x8000
/* pointer flags */
#define PTRFLAGS_MOVE     0x0800
#define PTRFLAGS_DOWN     0x8000
#define PTRFLAGS_BUTTON1  0x1000
#define PTRFLAGS_BUTTON2  0x2000
#define PTRFLAGS_BUTTON3  0x4000
#define PTRFLAGS_WHEEL    0x0200
#define PTRFLAGS_WHEEL_NEGATIVE 0x0100
#define PTRXFLAGS_BUTTON1 0x0001
#define PTRXFLAGS_BUTTON2 0x0002

/* fast-path input (2.2.8.1.2) */
#define FASTPATH_INPUT_EVENT_SCANCODE 0
#define FASTPATH_INPUT_EVENT_MOUSE    1
#define FASTPATH_INPUT_EVENT_MOUSEX   2
#define FASTPATH_INPUT_EVENT_SYNC     3
#define FASTPATH_INPUT_EVENT_UNICODE  4

/* Capability set types (2.2.7) */
#define CAPSET_GENERAL   1
#define CAPSET_BITMAP    2
#define CAPSET_ORDER     3
#define CAPSET_POINTER   8
#define CAPSET_SHARE     9
#define CAPSET_COLORCACHE 10
#define CAPSET_INPUT     13
#define CAPSET_FONT      14
#define CAPSET_VIRTUALCHANNEL 20
#define CAPSET_MULTIFRAGMENT 26
#define CAPSET_LARGE_POINTER 27

/* ── a growing / shrinking byte cursor over a caller's buffer ──────── */
typedef struct { uint8_t* p; uint32_t len, cap; int err; } rdp_w_t;
typedef struct { const uint8_t* p; uint32_t len, off; int err; } rdp_r_t;

static inline void rw_u8(rdp_w_t* w, uint8_t v) {
    if (w->len + 1 > w->cap) { w->err = 1; return; }
    w->p[w->len++] = v;
}
static inline void rw_bytes(rdp_w_t* w, const void* d, uint32_t n) {
    if (w->len + n > w->cap) { w->err = 1; return; }
    for (uint32_t i = 0; i < n; i++) w->p[w->len + i] = ((const uint8_t*)d)[i];
    w->len += n;
}
static inline void rw_le16(rdp_w_t* w, uint16_t v) { rw_u8(w, (uint8_t)v); rw_u8(w, (uint8_t)(v >> 8)); }
static inline void rw_le32(rdp_w_t* w, uint32_t v) { rw_le16(w, (uint16_t)v); rw_le16(w, (uint16_t)(v >> 16)); }
static inline void rw_be16(rdp_w_t* w, uint16_t v) { rw_u8(w, (uint8_t)(v >> 8)); rw_u8(w, (uint8_t)v); }

static inline uint8_t rr_u8(rdp_r_t* r) {
    if (r->off + 1 > r->len) { r->err = 1; return 0; }
    return r->p[r->off++];
}
static inline uint16_t rr_le16(rdp_r_t* r) {
    uint16_t v = (uint16_t)rr_u8(r);
    return v | (uint16_t)rr_u8(r) << 8;
}
static inline uint32_t rr_le32(rdp_r_t* r) {
    uint32_t v = rr_le16(r);
    return v | (uint32_t)rr_le16(r) << 16;
}
static inline uint16_t rr_be16(rdp_r_t* r) {
    uint16_t v = (uint16_t)rr_u8(r) << 8;
    return v | (uint16_t)rr_u8(r);
}
static inline void rr_skip(rdp_r_t* r, uint32_t n) {
    if (r->off + n > r->len) { r->err = 1; r->off = r->len; } else r->off += n;
}

#endif
