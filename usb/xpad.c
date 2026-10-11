#include "usbcore.h"
#include "kstring.h"
#include "kheap.h"
#include "serial.h"
#include "gamepad.h"

/*
 * Xbox controllers, which are not HID devices (a vendor-specific class):
 *  - Xbox 360 wired controllers and the many pads that copy them
 *    (class ff/5d/01): 20-byte input reports; the ring of lights is set
 *    to "player 1".
 *  - Xbox One / Series controllers (class ff/47/d0, "GIP"): they stay
 *    silent until told to start; input reports are command 0x20, the
 *    Xbox button its own command 0x07 (acknowledged when it asks).
 */

#define K_360 1
#define K_ONE 2

typedef struct {
    int        kind;
    int        pad;
    usb_xfer_t in, out;
    uint8_t    seq;
    uint32_t   guide;          /* (Xbox One: the button comes on its own) */
} xpad_t;

typedef struct {
    int           kind, in_iface;
    usb_ep_desc_t in, out;
    int           have_in, have_out;
} scan_t;

static int scan_cb(const uint8_t* desc, void* ctx) {
    scan_t* s = (scan_t*)ctx;
    if (desc[1] == USB_DT_INTERFACE) {
        const usb_iface_desc_t* id = (const usb_iface_desc_t*)desc;
        s->in_iface = 0;
        if (s->kind || id->bAlternateSetting != 0 || id->bInterfaceClass != 0xFF) return 0;
        if (id->bInterfaceSubClass == 0x5D && id->bInterfaceProtocol == 0x01) s->in_iface = K_360;
        else if (id->bInterfaceSubClass == 0x47 && id->bInterfaceProtocol == 0xD0) s->in_iface = K_ONE;
    } else if (desc[1] == USB_DT_ENDPOINT && s->in_iface) {
        const usb_ep_desc_t* ep = (const usb_ep_desc_t*)desc;
        if ((ep->bmAttributes & 3) != USB_EP_INTERRUPT) return 0;
        if ((ep->bEndpointAddress & 0x80) && !s->have_in) { s->in = *ep; s->have_in = 1; }
        else if (!(ep->bEndpointAddress & 0x80) && !s->have_out) { s->out = *ep; s->have_out = 1; }
        if (s->have_in && (s->have_out || s->in_iface == K_360)) s->kind = s->in_iface;
    }
    return 0;
}

static int xpad_probe(usb_device_t* d) {
    scan_t s;
    memset(&s, 0, sizeof(s));
    usb_walk_config(d, 0, scan_cb, &s);
    return s.kind ? 0 : -1;
}

/* a message to the controller (one at a time: dropped if one is under way) */
static void send_out(xpad_t* x, const uint8_t* msg, uint32_t len) {
    if (!x->out.buf || x->out.status == USB_XFER_PENDING || len > 64) return;
    memcpy(x->out.buf, msg, len);
    x->out.len = len;
    usb_submit(&x->out);
}

static int xpad_attach(usb_device_t* d) {
    scan_t s;
    memset(&s, 0, sizeof(s));
    usb_walk_config(d, d->active_config, scan_cb, &s);
    if (!s.kind) return -1;
    xpad_t* x = (xpad_t*)kzalloc(sizeof(xpad_t));
    if (!x) return -1;
    x->kind = s.kind;
    if (usb_open_endpoint(d, &s.in) != 0) { kfree(x); return -1; }
    x->in.dev = d;
    x->in.ep = s.in.bEndpointAddress;
    x->in.buf = (uint8_t*)usb_dma_alloc(64);
    uint16_t mps = s.in.wMaxPacketSize & 0x7FF;
    x->in.len = mps && mps <= 64 ? mps : 32;
    if (!x->in.buf) { kfree(x); return -1; }
    if (s.have_out && usb_open_endpoint(d, &s.out) == 0) {
        x->out.dev = d;
        x->out.ep = s.out.bEndpointAddress;
        x->out.buf = (uint8_t*)usb_dma_alloc(64);
    }
    d->drvdata = x;
    if (x->kind == K_ONE) {
        static const uint8_t start[5] = { 0x05, 0x20, 0x00, 0x01, 0x00 };   /* power on: send input */
        send_out(x, start, sizeof(start));
        x->seq = 1;
    } else {
        static const uint8_t led[3] = { 0x01, 0x03, 0x06 };                 /* the ring: player 1 */
        send_out(x, led, sizeof(led));
    }
    x->pad = gamepad_add(d->product[0] ? d->product : x->kind == K_ONE ? "Xbox One controller" : "Xbox 360 controller");
    usb_submit(&x->in);
    klog("xpad: %s on endpoint %02x\n", x->kind == K_ONE ? "Xbox One controller" : "Xbox 360 controller", x->in.ep);
    return 0;
}

static int s16(const uint8_t* p) { return (int16_t)(p[0] | p[1] << 8); }
static int clamp_neg(int v) { v = -v; return v > 32767 ? 32767 : v; }
static int clamp(int v) { return v < -32767 ? -32767 : v; }

static void report_360(xpad_t* x, const uint8_t* b, uint32_t n) {
    if (n < 14 || b[0] != 0x00 || b[1] < 0x14) return;    /* (0x01: the lights, 0x03: rumble state...) */
    static const uint32_t MAP[16] = { GP_UP, GP_DOWN, GP_LEFT, GP_RIGHT, GP_START, GP_BACK, GP_LS, GP_RS,
                                      GP_LB, GP_RB, GP_GUIDE, 0, GP_A, GP_B, GP_X, GP_Y };
    uint16_t bt = (uint16_t)(b[2] | b[3] << 8);
    gamepad_t s;
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 16; i++) if (bt & (1u << i)) s.buttons |= MAP[i];
    s.lt = b[4] * 32767 / 255;
    s.rt = b[5] * 32767 / 255;
    s.lx = clamp(s16(b + 6));
    s.ly = clamp_neg(s16(b + 8));                          /* (up is + on the wire) */
    s.rx = clamp(s16(b + 10));
    s.ry = clamp_neg(s16(b + 12));
    gamepad_update(x->pad, &s);
}

static void report_one(xpad_t* x, const uint8_t* b, uint32_t n) {
    if (n >= 4 && (b[1] & 0x10)) {                         /* it wants an acknowledgement */
        uint8_t ack[13] = { 0x01, 0x20, b[2], 0x09, 0x00, b[0], 0x20, b[3], 0x00, 0x00, 0x00, 0x00, 0x00 };
        send_out(x, ack, sizeof(ack));
    }
    if (b[0] == 0x07 && n >= 5) {                          /* the Xbox button */
        x->guide = (b[4] & 1) ? GP_GUIDE : 0;
        gamepad_t cur;
        if (gamepad_get(x->pad, &cur)) {
            cur.buttons = (cur.buttons & ~GP_GUIDE) | x->guide;
            gamepad_update(x->pad, &cur);
        }
        return;
    }
    if (b[0] != 0x20 || n < 18) return;
    gamepad_t s;
    memset(&s, 0, sizeof(s));
    static const uint32_t B4[8] = { 0, 0, GP_START, GP_BACK, GP_A, GP_B, GP_X, GP_Y };
    static const uint32_t B5[8] = { GP_UP, GP_DOWN, GP_LEFT, GP_RIGHT, GP_LB, GP_RB, GP_LS, GP_RS };
    for (int i = 0; i < 8; i++) {
        if (b[4] & (1u << i)) s.buttons |= B4[i];
        if (b[5] & (1u << i)) s.buttons |= B5[i];
    }
    s.buttons |= x->guide;
    int lt = b[6] | b[7] << 8, rt = b[8] | b[9] << 8;
    s.lt = (lt > 1023 ? 1023 : lt) * 32767 / 1023;
    s.rt = (rt > 1023 ? 1023 : rt) * 32767 / 1023;
    s.lx = clamp(s16(b + 10));
    s.ly = clamp_neg(s16(b + 12));
    s.rx = clamp(s16(b + 14));
    s.ry = clamp_neg(s16(b + 16));
    gamepad_update(x->pad, &s);
}

static void xpad_poll(usb_device_t* d) {
    xpad_t* x = (xpad_t*)d->drvdata;
    if (!x || x->in.status == USB_XFER_PENDING || x->in.status == USB_XFER_GONE) return;
    if (x->in.status == USB_XFER_OK) {
        if (x->kind == K_360) report_360(x, x->in.buf, x->in.actual);
        else report_one(x, x->in.buf, x->in.actual);
    }
    usb_submit(&x->in);
}

static void xpad_detach(usb_device_t* d) {
    xpad_t* x = (xpad_t*)d->drvdata;
    if (!x) return;
    gamepad_remove(x->pad);
    d->drvdata = NULL;
}

const usb_driver_t xpad_driver = {
    "xpad", xpad_probe, xpad_attach, xpad_poll, xpad_detach,
};
