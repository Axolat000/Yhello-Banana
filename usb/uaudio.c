/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "usbcore.h"
#include "kstring.h"
#include "kheap.h"
#include "serial.h"
#include "audio.h"

/*
 * USB audio (USB Audio Class 1.0): headsets, USB speakers, USB sound
 * cards and DACs - full-speed ones, the class every system drives without
 * a driver of its own. While one is plugged in, sound goes to it.
 *
 * Its streaming interface's alternate setting with 16-bit stereo PCM at
 * 48 kHz is chosen; then every millisecond (each service interval) one
 * isochronous packet of the ring's next bytes goes out. A few dozen
 * packets are queued at the controller; as they finish (looked at from
 * the timer interrupt, like the PCI cards' rings: audio.c's tick) they
 * are refilled from the ring and queued again. The feature unit is put
 * to 0 dB, unmuted. xHCI controllers only (isochronous transfers).
 */

#define NPKT     32                       /* packets queued (~32 ms at 1 ms each) */
#define SLOT     256                      /* bytes per packet buffer (never crosses 64 KiB) */
#define RATE     48000

typedef struct {
    int           ok;
    uint8_t       ac_iface, as_iface, alt;
    usb_ep_desc_t ep;
    int           cur_as, cur_alt, cur_num, cur_fmt_ok, cur_ac;
    uint8_t       fu_id;                  /* a feature unit, 0 if none */
    int           freq_ctl;
} scan_t;

typedef struct {
    usb_device_t* dev;
    usb_xfer_t    x[NPKT];
    uint8_t*      bufs;
    uint32_t      bytes;                  /* per packet */
    uint32_t      head;                   /* the oldest packet queued */
    uint64_t      played, sent;           /* ring bytes */
    volatile int  on;
    char          name[96];
} ua_t;

static ua_t* g_ua;                        /* the one playing */

static int has_48k(const uint8_t* d) {
    int n = d[7];
    if (n == 0) {                         /* a continuous range */
        if (d[0] < 14) return 0;
        uint32_t lo = d[8] | d[9] << 8 | (uint32_t)d[10] << 16, hi = d[11] | d[12] << 8 | (uint32_t)d[13] << 16;
        return lo <= RATE && RATE <= hi;
    }
    for (int i = 0; i < n && 8 + i * 3 + 2 < d[0]; i++) {
        const uint8_t* f = d + 8 + i * 3;
        if ((uint32_t)(f[0] | f[1] << 8 | f[2] << 16) == RATE) return 1;
    }
    return 0;
}

static int scan_cb(const uint8_t* d, void* ctx) {
    scan_t* s = (scan_t*)ctx;
    if (d[1] == USB_DT_INTERFACE) {
        const usb_iface_desc_t* id = (const usb_iface_desc_t*)d;
        s->cur_as = s->cur_ac = 0;
        s->cur_fmt_ok = 0;
        if (id->bInterfaceClass != 1 || id->bInterfaceProtocol != 0) return 0;   /* (UAC2: protocol 0x20) */
        if (id->bInterfaceSubClass == 1) { s->cur_ac = 1; s->ac_iface = id->bInterfaceNumber; }
        if (id->bInterfaceSubClass == 2 && id->bAlternateSetting != 0) {
            s->cur_as = 1;
            s->cur_alt = id->bAlternateSetting;
            s->cur_num = id->bInterfaceNumber;
        }
    } else if (d[1] == 0x24 && s->cur_ac && d[0] >= 6 && d[2] == 0x06 && !s->fu_id) {
        s->fu_id = d[3];                                    /* FEATURE_UNIT */
    } else if (d[1] == 0x24 && s->cur_as && d[0] >= 8 && d[2] == 0x02) {
        /* FORMAT_TYPE I: 2 channels, 2-byte subframes, 16 bits, 48 kHz */
        s->cur_fmt_ok = d[3] == 1 && d[4] == 2 && d[5] == 2 && d[6] == 16 && has_48k(d);
    } else if (d[1] == USB_DT_ENDPOINT && s->cur_as && s->cur_fmt_ok && !s->ok) {
        const usb_ep_desc_t* ep = (const usb_ep_desc_t*)d;
        if ((ep->bmAttributes & 3) != USB_EP_ISOCH || (ep->bEndpointAddress & 0x80)) return 0;
        s->ep = *ep;
        s->alt = (uint8_t)s->cur_alt;
        s->as_iface = (uint8_t)s->cur_num;
        s->ok = 1;
    } else if (d[1] == 0x25 && s->ok && d[0] >= 3 && d[2] == 0x01 && !s->freq_ctl) {
        s->freq_ctl = d[3] & 1;                             /* EP_GENERAL: sampling frequency control */
    }
    return 0;
}

static int ua_probe(usb_device_t* d) {
    scan_t s;
    memset(&s, 0, sizeof(s));
    usb_walk_config(d, 0, scan_cb, &s);
    return s.ok ? 0 : -1;
}

/* packet k: the ring's next bytes */
static void fill(ua_t* u, int k) {
    const uint8_t* ring = audio_ring();
    uint32_t rb = audio_ring_bytes();
    uint32_t at = (uint32_t)(u->sent % rb);
    uint8_t* b = u->bufs + (uint32_t)k * SLOT;
    uint32_t first = rb - at < u->bytes ? rb - at : u->bytes;
    memcpy(b, ring + at, first);
    if (first < u->bytes) memcpy(b + first, ring, u->bytes - first);
    u->sent += u->bytes;
    u->x[k].len = u->bytes;
    usb_submit(&u->x[k]);
}

static int card_start(void) {
    ua_t* u = g_ua;
    if (!u) return -1;
    u->played = u->sent = 0;
    u->head = 0;
    for (int k = 0; k < NPKT; k++) fill(u, k);
    u->on = 1;
    return 0;
}

static uint32_t card_position(void) {
    ua_t* u = g_ua;
    return u ? (uint32_t)(u->played % audio_ring_bytes()) : 0;
}

/* from the timer interrupt: finished packets go out again with new sound */
static void card_tick(void) {
    ua_t* u = g_ua;
    if (!u || !u->on || !u->dev->present) return;
    if (u->dev->hc->ops->events) u->dev->hc->ops->events(u->dev->hc);
    for (int n = 0; n < NPKT; n++) {
        usb_xfer_t* x = &u->x[u->head];
        if (x->status == USB_XFER_PENDING) break;
        if (x->status == USB_XFER_GONE) { u->on = 0; return; }
        u->played += u->bytes;
        fill(u, (int)u->head);
        u->head = (u->head + 1) % NPKT;
    }
}

static void card_stop(void) {
    if (g_ua) g_ua->on = 0;                 /* (the queued packets finish on their own) */
}

static const audio_card_t g_card = { "USB audio", card_start, card_position, card_tick, card_stop };

static int ua_attach(usb_device_t* d) {
    if (g_ua || !d->hc->ops->events) return -1;            /* one at a time; xHCI only */
    scan_t s;
    memset(&s, 0, sizeof(s));
    usb_walk_config(d, d->active_config, scan_cb, &s);
    if (!s.ok) return -1;
    /* the packet: one service interval of sound */
    uint32_t b = s.ep.bInterval ? s.ep.bInterval : 1;
    uint32_t period_us = (d->speed == USB_SPEED_HIGH ? 125u : 1000u) << (b - 1 > 10 ? 10 : b - 1);
    uint32_t bytes = (uint32_t)((uint64_t)RATE * 4 * period_us / 1000000u);
    uint16_t mps = s.ep.wMaxPacketSize & 0x7FF;
    if (bytes == 0 || bytes % 4 || bytes > mps || bytes > SLOT) {
        klog("uaudio: unusable packet size (%u bytes every %u us, max %u)\n", bytes, period_us, mps);
        return -1;
    }
    ua_t* u = (ua_t*)kzalloc(sizeof(ua_t));
    if (!u) return -1;
    u->dev = d;
    u->bytes = bytes;
    u->bufs = (uint8_t*)usb_dma_alloc(NPKT * SLOT);
    if (!u->bufs) { kfree(u); return -1; }
    /* the streaming setting, 48 kHz, the volume at 0 dB and unmuted */
    usb_control(d, USB_RECIP_IFACE, 0x0B /* SET_INTERFACE */, s.alt, s.as_iface, NULL, 0);
    uint8_t rate[3] = { RATE & 0xFF, (RATE >> 8) & 0xFF, RATE >> 16 };
    usb_control(d, USB_TYPE_CLASS | 0x02 /* endpoint */, 0x01 /* SET_CUR */, 0x0100, s.ep.bEndpointAddress, rate, 3);
    if (s.fu_id) {
        uint8_t unmute = 0, vol[2] = { 0, 0 };
        uint16_t where = (uint16_t)(s.fu_id << 8 | s.ac_iface);
        usb_control(d, USB_TYPE_CLASS | USB_RECIP_IFACE, 0x01, 0x0100, where, &unmute, 1);   /* mute, master */
        for (int ch = 0; ch <= 2; ch++)
            usb_control(d, USB_TYPE_CLASS | USB_RECIP_IFACE, 0x01, (uint16_t)(0x0200 | ch), where, vol, 2);   /* volume */
    }
    if (usb_open_endpoint(d, &s.ep) != 0) { klog("uaudio: the stream endpoint could not be opened\n"); kfree(u); return -1; }
    for (int k = 0; k < NPKT; k++) {
        u->x[k].dev = d;
        u->x[k].ep = s.ep.bEndpointAddress;
        u->x[k].buf = u->bufs + k * SLOT;
    }
    ksnprintf(u->name, sizeof(u->name), "USB audio: %s", d->product[0] ? d->product : "USB sound device");
    d->drvdata = u;
    g_ua = u;
    klog("uaudio: %s, 48 kHz 16-bit stereo, %u-byte packets every %u us\n", u->name, bytes, period_us);
    audio_card_plug(&g_card, u->name);
    return 0;
}

static void ua_poll(usb_device_t* d) { (void)d; }          /* (fed from the timer: card_tick) */

static void ua_detach(usb_device_t* d) {
    ua_t* u = (ua_t*)d->drvdata;
    if (!u) return;
    u->on = 0;
    audio_card_unplug(&g_card);
    if (g_ua == u) g_ua = NULL;
    d->drvdata = NULL;
}

const usb_driver_t uaudio_driver = {
    "uaudio", ua_probe, ua_attach, ua_poll, ua_detach,
};
