#include "usbcore.h"
#include "adbdev.h"
#include "kstring.h"
#include "kheap.h"
#include "timer.h"
#include "serial.h"

/*
 * The "phone driver": binds a phone's ADB or fastboot interface (Android
 * phones, tablets, TVs - any maker: the interface class is the same) and
 * moves bytes. Reading keeps one IN transfer queued at all times, so a
 * read that gives up waiting (nothing from the phone yet) loses nothing:
 * the next read finds that same transfer.
 */

#define CHUNK 16384                      /* bytes per transfer (a multiple of every packet size) */

typedef struct {
    int           used, kind;
    usb_device_t* dev;
    uint8_t       ep_in, ep_out;
    usb_xfer_t    rx, tx;
    int           rx_on;                 /* rx submitted */
    uint8_t*      rxbuf;
    uint8_t*      txbuf;
    uint32_t      have, at;              /* bytes of rxbuf not handed out yet */
    adbdev_info_t info;
} adev_t;

static adev_t g_dev[ADBDEV_MAX];

typedef struct {
    int           kind, cur, iface;
    usb_ep_desc_t in, out;
    int           have_in, have_out;
} scan_t;

static int scan_cb(const uint8_t* d, void* ctx) {
    scan_t* s = (scan_t*)ctx;
    if (d[1] == USB_DT_INTERFACE) {
        const usb_iface_desc_t* id = (const usb_iface_desc_t*)d;
        s->cur = 0;
        if (s->kind || id->bAlternateSetting) return 0;
        if (id->bInterfaceClass == 0xFF && id->bInterfaceSubClass == 0x42 &&
            (id->bInterfaceProtocol == ADBDEV_ADB || id->bInterfaceProtocol == ADBDEV_FASTBOOT)) {
            s->cur = id->bInterfaceProtocol;
            s->iface = id->bInterfaceNumber;
            s->have_in = s->have_out = 0;
        }
    } else if (d[1] == USB_DT_ENDPOINT && s->cur) {
        const usb_ep_desc_t* ep = (const usb_ep_desc_t*)d;
        if ((ep->bmAttributes & 3) != USB_EP_BULK) return 0;
        if (ep->bEndpointAddress & 0x80) { if (!s->have_in) { s->in = *ep; s->have_in = 1; } }
        else if (!s->have_out) { s->out = *ep; s->have_out = 1; }
        if (s->have_in && s->have_out) s->kind = s->cur;
    }
    return 0;
}

static int ad_probe(usb_device_t* d) {
    scan_t s;
    memset(&s, 0, sizeof(s));
    usb_walk_config(d, 0, scan_cb, &s);
    return s.kind ? 0 : -1;
}

static int ad_attach(usb_device_t* d) {
    scan_t s;
    memset(&s, 0, sizeof(s));
    usb_walk_config(d, d->active_config, scan_cb, &s);
    if (!s.kind) return -1;
    adev_t* a = NULL;
    for (int i = 0; i < ADBDEV_MAX; i++) if (!g_dev[i].used) { a = &g_dev[i]; break; }
    if (!a) return -1;
    if (usb_open_endpoint(d, &s.in) != 0 || usb_open_endpoint(d, &s.out) != 0) return -1;
    if (!a->rxbuf) a->rxbuf = (uint8_t*)usb_dma_alloc(CHUNK);
    if (!a->txbuf) a->txbuf = (uint8_t*)usb_dma_alloc(CHUNK);
    if (!a->rxbuf || !a->txbuf) return -1;
    a->kind = s.kind;
    a->dev = d;
    a->ep_in = s.in.bEndpointAddress;
    a->ep_out = s.out.bEndpointAddress;
    a->rx_on = 0;
    a->have = a->at = 0;
    memset(&a->info, 0, sizeof(a->info));
    a->info.kind = s.kind;
    a->info.vid = d->desc.idVendor;
    a->info.pid = d->desc.idProduct;
    if (!d->desc.iSerialNumber || usb_get_string(d, d->desc.iSerialNumber, a->info.serial, sizeof(a->info.serial)) != 0 || !a->info.serial[0])
        ksnprintf(a->info.serial, sizeof(a->info.serial), "%04x%04x", d->desc.idVendor, d->desc.idProduct);
    kstrlcpy(a->info.product, d->product, sizeof(a->info.product));
    kstrlcpy(a->info.maker, d->manufacturer, sizeof(a->info.maker));
    a->used = 1;
    d->drvdata = a;
    klog("adb: %s %s (%s) - %s\n", a->info.maker, a->info.product, a->info.serial,
         s.kind == ADBDEV_ADB ? "USB debugging (adb)" : "bootloader (fastboot)");
    return 0;
}

static void ad_poll(usb_device_t* d) { (void)d; }

static void ad_detach(usb_device_t* d) {
    adev_t* a = (adev_t*)d->drvdata;
    if (!a) return;
    /* (rx / tx may still be queued: the controller marks them gone; the
     * buffers stay allocated for the slot's next phone) */
    a->used = 0;
    a->dev = NULL;
    d->drvdata = NULL;
    klog("adb: %s unplugged\n", a->info.serial);
}

const usb_driver_t adb_driver = { "adb", ad_probe, ad_attach, ad_poll, ad_detach };

/* ── the commands' side ───────────────────────────────────────────── */

int adbdev_list(adbdev_info_t* out, int max) {
    int n = 0;
    for (int i = 0; i < ADBDEV_MAX && n < max; i++) if (g_dev[i].used) out[n++] = g_dev[i].info;
    return n;
}

int adbdev_open(int kind, const char* serial) {
    for (int i = 0; i < ADBDEV_MAX; i++)
        if (g_dev[i].used && g_dev[i].kind == kind && (!serial || !serial[0] || !strcmp(serial, g_dev[i].info.serial))) return i;
    return -1;
}

int adbdev_present(int h) {
    return h >= 0 && h < ADBDEV_MAX && g_dev[h].used && g_dev[h].dev && g_dev[h].dev->present;
}

/* waits for a transfer: 1 done, 0 still going after timeout_ms, -1 gone */
static int wait_xfer(adev_t* a, usb_xfer_t* x, uint32_t timeout_ms) {
    uint32_t t0 = timer_ms();
    while (x->status == USB_XFER_PENDING) {
        if (!a->used || !a->dev || !a->dev->present) return -1;
        if (timer_ms() - t0 >= timeout_ms) return 0;
        a->dev->hc->ops->poll(a->dev->hc);
        usb_wait();
    }
    return 1;
}

int adbdev_write(int h, const void* buf, uint32_t len) {
    if (!adbdev_present(h)) return -1;
    adev_t* a = &g_dev[h];
    const uint8_t* p = (const uint8_t*)buf;
    uint32_t done = 0;
    while (done < len) {
        uint32_t c = len - done > CHUNK ? CHUNK : len - done;
        memcpy(a->txbuf, p + done, c);
        memset(&a->tx, 0, sizeof(a->tx));
        a->tx.dev = a->dev;
        a->tx.ep = a->ep_out;
        a->tx.buf = a->txbuf;
        a->tx.len = c;
        if (usb_submit(&a->tx) != 0) return -1;
        int r = wait_xfer(a, &a->tx, 10000);
        if (r <= 0 || a->tx.status != USB_XFER_OK) {
            if (r == 0) klog("adb: write timed out\n");
            return -1;
        }
        done += c;
    }
    return (int)len;
}

int adbdev_read(int h, void* buf, uint32_t len, uint32_t timeout_ms) {
    if (!adbdev_present(h)) return -1;
    adev_t* a = &g_dev[h];
    if (a->have == 0) {
        if (!a->rx_on) {
            memset(&a->rx, 0, sizeof(a->rx));
            a->rx.dev = a->dev;
            a->rx.ep = a->ep_in;
            a->rx.buf = a->rxbuf;
            a->rx.len = CHUNK;
            if (usb_submit(&a->rx) != 0) return -1;
            a->rx_on = 1;
        }
        int r = wait_xfer(a, &a->rx, timeout_ms);
        if (r < 0) return -1;
        if (r == 0) return 0;
        a->rx_on = 0;
        if (a->rx.status != USB_XFER_OK) return -1;
        a->have = a->rx.actual;
        a->at = 0;
        if (!a->have) return 0;          /* (a zero-length packet) */
    }
    uint32_t n = len < a->have ? len : a->have;
    memcpy(buf, a->rxbuf + a->at, n);
    a->at += n;
    a->have -= n;
    return (int)n;
}

void adbdev_flush_input(int h) {
    if (h >= 0 && h < ADBDEV_MAX) g_dev[h].have = 0;
}
