/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "usbcore.h"
#include "pci.h"
#include "kstring.h"
#include "kheap.h"
#include "timer.h"
#include "serial.h"

/*
 * OHCI (Open Host Controller Interface, USB 1.1) driver: the USB 1.1
 * controllers of AMD, SiS, ALi, NEC and Apple chipsets, many add-in cards -
 * and VirtualBox's default USB controller (its "USB Tablet" pointer, the
 * one that follows the host's mouse, sits on it).
 *
 * The controller walks three lists of endpoint descriptors (EDs): control,
 * bulk, and the periodic one the HCCA's interrupt table points at (here one
 * anchor ED for every frame, so interrupt endpoints are polled each
 * millisecond). Each ED holds a queue of general transfer descriptors
 * (TDs) ending at a dummy one; unlike UHCI a TD is a whole buffer (up to
 * two pages), split into packets by the controller. Everything is polled
 * (usb_poll): a transfer is over when its ED's queue is empty (head ==
 * tail), halted (an error), or one of its TDs came back short.
 *
 * Taken over from the BIOS / SMM (OwnershipChangeRequest), reset, run with
 * its interrupts off. Section numbers: OHCI specification 1.0a.
 */

#define R_REVISION   0x00
#define R_CONTROL    0x04
#define R_CMDSTATUS  0x08
#define R_INTSTATUS  0x0C
#define R_INTENABLE  0x10
#define R_INTDISABLE 0x14
#define R_HCCA       0x18
#define R_CTRLHEAD   0x20
#define R_CTRLCUR    0x24
#define R_BULKHEAD   0x28
#define R_BULKCUR    0x2C
#define R_FMINTERVAL 0x34
#define R_PERSTART   0x40
#define R_LSTHRESH   0x44
#define R_RHDESCA    0x48
#define R_RHDESCB    0x4C
#define R_RHSTATUS   0x50
#define R_PORT(p)    (0x54 + 4 * ((p) - 1))

#define CTL_CBSR3    0x003
#define CTL_PLE      0x004
#define CTL_IE       0x008
#define CTL_CLE      0x010
#define CTL_BLE      0x020
#define CTL_HCFS     0x0C0
#define CTL_OPER     0x080
#define CTL_RESUME   0x040
#define CTL_IR       0x100

#define CS_HCR 0x01
#define CS_CLF 0x02
#define CS_BLF 0x04
#define CS_OCR 0x08

/* root hub port status (read) / commands (write) */
#define P_CCS  (1u << 0)
#define P_PES  (1u << 1)
#define P_PRS  (1u << 4)
#define P_PPS  (1u << 8)
#define P_LSDA (1u << 9)
#define P_CSC  (1u << 16)
#define P_PESC (1u << 17)
#define P_PRSC (1u << 20)
#define P_WC   (0x1Fu << 16)
#define W_CLEAR_ENABLE (1u << 0)
#define W_SET_RESET    (1u << 4)
#define W_SET_POWER    (1u << 8)

/* ED dword 0 */
#define ED_LOWSPEED (1u << 13)
#define ED_SKIP     (1u << 14)
#define ED_DIR_TD   (0u << 11)
#define ED_HALTED   1u
#define ED_CARRY    2u

/* TD dword 0 */
#define TD_ROUND    (1u << 18)
#define TD_SETUP    (0u << 19)
#define TD_OUT      (1u << 19)
#define TD_IN       (2u << 19)
#define TD_NOINT    (7u << 21)
#define TD_DATA0    (2u << 24)
#define TD_DATA1    (3u << 24)
#define TD_CC_NEW   (15u << 28)                     /* "not accessed" */
#define CC_STALL    4

#define MAX_OHCI 8
#define MAX_ODEV 16
#define MAX_TD   8

typedef struct __attribute__((packed, aligned(16))) { volatile uint32_t ctrl, tail, head, next; } oed_t;
typedef struct __attribute__((packed, aligned(16))) { volatile uint32_t ctrl, cbp, next, be; } otd_t;

typedef struct {
    oed_t*      ed;
    uint8_t*    ed_raw;
    otd_t*      idle;                              /* the dummy TD an empty queue points at */
    uint8_t*    idle_raw;
    otd_t*      tds;                               /* the transfer under way */
    uint8_t*    tds_raw;
    int         ntd;
    uint32_t    start[MAX_TD], len[MAX_TD];
    usb_xfer_t* xfer;
    uint8_t     ep;
    uint16_t    mps;
    int         type;                              /* USB_EP_* (control for EP0) */
} oep_t;

typedef struct ohci ohci_t;

typedef struct {
    usb_device_t* udev;
    uint8_t       port, addr, low;
    uint8_t*      ctrl_buf;                        /* setup packet (64 B) + data (4 KiB) */
    oep_t*        ep[32];
} odev_t;

struct ohci {
    usb_hc_t        hc;
    volatile uint8_t* mmio;
    int             ports;
    uint8_t*        hcca;
    oed_t*          ctrl_anchor;
    oed_t*          bulk_anchor;
    oed_t*          intr_anchor;
    odev_t*         port_dev[16];
    odev_t*         devs[MAX_ODEV];
    uint8_t         next_addr;
};

static ohci_t* g_ohci[MAX_OHCI];
static int     g_ohci_count;

static uint32_t rd(ohci_t* o, int r) { return *(volatile uint32_t*)(o->mmio + r); }
static void     wr(ohci_t* o, int r, uint32_t v) { *(volatile uint32_t*)(o->mmio + r) = v; }

static void* dma16(uint32_t size, uint8_t** raw) {
    *raw = (uint8_t*)kmalloc(size + 16);
    if (!*raw) return NULL;
    uint8_t* p = (uint8_t*)(((uintptr_t)*raw + 15) & ~(uintptr_t)15);
    memset(p, 0, size);
    return p;
}

static uint32_t phys(const volatile void* p) { return (uint32_t)(uintptr_t)p; }

/* ── endpoints ──────────────────────────────────────────────────── */

static oed_t* anchor_of(ohci_t* o, int type) {
    if (type == USB_EP_INTERRUPT) return o->intr_anchor;
    if (type == USB_EP_BULK) return o->bulk_anchor;
    return o->ctrl_anchor;
}

static void ep_set_addr(oep_t* e, odev_t* od) {
    e->ed->ctrl = (od->addr & 0x7Fu) | ((uint32_t)(e->ep & 0x0F) << 7) | ED_DIR_TD |
                  (od->low ? ED_LOWSPEED : 0) | ((uint32_t)(e->mps & 0x7FF) << 16);
}

static oep_t* ep_new(ohci_t* o, odev_t* od, uint8_t ep, uint16_t mps, int type) {
    oep_t* e = (oep_t*)kzalloc(sizeof(oep_t));
    if (!e) return NULL;
    e->ed = (oed_t*)dma16(sizeof(oed_t), &e->ed_raw);
    e->idle = (otd_t*)dma16(sizeof(otd_t), &e->idle_raw);
    if (!e->ed || !e->idle) { if (e->ed_raw) kfree(e->ed_raw); if (e->idle_raw) kfree(e->idle_raw); kfree(e); return NULL; }
    e->ep = ep;
    e->mps = mps ? mps : 8;
    e->type = type;
    ep_set_addr(e, od);
    e->ed->tail = phys(e->idle);
    e->ed->head = phys(e->idle);                   /* empty, toggle DATA0 */
    oed_t* a = anchor_of(o, type);
    e->ed->next = a->next;
    __asm__ volatile("" ::: "memory");
    a->next = phys(e->ed);
    return e;
}

static void ep_unlink(ohci_t* o, oep_t* e) {
    e->ed->ctrl |= ED_SKIP;
    oed_t* p = anchor_of(o, e->type);
    for (int guard = 0; guard < 256 && p->next; guard++) {
        if (p->next == phys(e->ed)) { p->next = e->ed->next; break; }
        p = (oed_t*)(uintptr_t)(p->next & ~0xFu);
    }
    usb_delay_ms(2);                               /* (the controller may still hold it this frame) */
}

static void ep_drop_tds(oep_t* e) {
    if (e->tds_raw) kfree(e->tds_raw);
    e->tds_raw = NULL;
    e->tds = NULL;
    e->ntd = 0;
}

/* the ED's queue back to empty (after a halt or a short packet), keeping
 * its data toggle unless it halted (a STALL resets it on both sides) */
static void ep_reset_queue(oep_t* e, int halted) {
    e->ed->ctrl |= ED_SKIP;
    usb_delay_ms(1);
    uint32_t carry = halted ? 0 : (e->ed->head & ED_CARRY);
    e->ed->head = phys(e->idle) | carry;
    e->ed->ctrl &= ~ED_SKIP;
}

static void td_fill(otd_t* t, uint32_t flags, uint32_t buf, uint32_t len, uint32_t next) {
    t->ctrl = flags | TD_NOINT | TD_CC_NEW;
    t->cbp = len ? buf : 0;
    t->be = len ? buf + len - 1 : 0;
    t->next = next;
}

/* the queue under way: 0 running, 1 done (*status, *actual of the data TDs from first) */
static int ep_check(oep_t* e, int first_data, int* status, uint32_t* actual) {
    uint32_t head = e->ed->head;
    int halted = (head & ED_HALTED) != 0;
    int empty = (head & ~0xFu) == (e->ed->tail & ~0xFu);
    uint32_t got = 0;
    int short_in = 0;
    for (int i = 0; i < e->ntd; i++) {
        otd_t* t = &e->tds[i];
        uint32_t cc = t->ctrl >> 28;
        if (cc == 15) break;                       /* not reached (yet) */
        if (cc != 0) {
            if (!halted && !empty) return 0;       /* (not written back to the ED yet) */
            *status = cc == CC_STALL ? USB_XFER_STALL : USB_XFER_ERROR;
            *actual = got;
            return 1;
        }
        if (i >= first_data && e->len[i]) {
            uint32_t n = t->cbp ? t->cbp - e->start[i] : e->len[i];
            got += n;
            if (n < e->len[i]) short_in = 1;
        }
    }
    if (halted) { *status = USB_XFER_ERROR; *actual = got; return 1; }
    if (!empty && !short_in) return 0;
    *status = USB_XFER_OK;
    *actual = got;
    return empty ? 1 : 2;                          /* 2: short, TDs left behind */
}

static int ohci_control(usb_device_t* d, const usb_setup_t* s, void* data, uint32_t timeout_ms) {
    ohci_t* o = (ohci_t*)d->hc->priv;
    odev_t* od = (odev_t*)d->hcpriv;
    oep_t* e = od->ep[0];
    uint32_t len = s->wLength;
    int in = (s->bmRequestType & USB_DIR_IN) != 0;
    if (len > 4096 || !od->ctrl_buf) return -1;
    uint8_t* sb = od->ctrl_buf;
    memcpy(sb, s, 8);
    uint8_t* db = sb + 64;
    if (!in && len) memcpy(db, data, len);
    ep_set_addr(e, od);                            /* (the address changes after SET_ADDRESS) */
    e->tds = (otd_t*)dma16(3 * sizeof(otd_t), &e->tds_raw);
    if (!e->tds) return -1;
    int n = 0;
    e->start[n] = phys(sb); e->len[n] = 8;
    td_fill(&e->tds[n], TD_SETUP | TD_DATA0, phys(sb), 8, 0);
    n++;
    int first = n;
    if (len) {
        e->start[n] = phys(db); e->len[n] = len;
        td_fill(&e->tds[n], (in ? TD_IN : TD_OUT) | TD_DATA1 | TD_ROUND, phys(db), len, 0);
        n++;
    }
    e->start[n] = 0; e->len[n] = 0;
    td_fill(&e->tds[n], ((len && in) ? TD_OUT : TD_IN) | TD_DATA1, 0, 0, 0);
    n++;
    for (int i = 0; i + 1 < n; i++) e->tds[i].next = phys(&e->tds[i + 1]);
    e->tds[n - 1].next = phys(e->idle);
    e->ntd = n;
    __asm__ volatile("" ::: "memory");
    e->ed->head = phys(&e->tds[0]);
    wr(o, R_CMDSTATUS, CS_CLF);

    int status = USB_XFER_ERROR;
    uint32_t actual = 0, t0 = timer_ms();
    for (;;) {
        int r = ep_check(e, first, &status, &actual);
        if (r == 1) break;
        if (r == 2) {                              /* a short IN data stage: the status stage follows by itself */
            if (timer_ms() - t0 > timeout_ms) break;
            usb_wait();
            continue;
        }
        if (timer_ms() - t0 > timeout_ms) { status = USB_XFER_ERROR; break; }
        usb_wait();
    }
    int halted = (e->ed->head & ED_HALTED) != 0;
    if (halted || (e->ed->head & ~0xFu) != phys(e->idle)) ep_reset_queue(e, 1);
    ep_drop_tds(e);
    if (status != USB_XFER_OK) return -1;
    if (in && actual) memcpy(data, db, actual > len ? len : actual);
    return (int)actual;
}

static int ohci_set_ep0_mps(usb_device_t* d, uint16_t mps) {
    odev_t* od = (odev_t*)d->hcpriv;
    od->ep[0]->mps = mps;
    ep_set_addr(od->ep[0], od);
    return 0;
}

static int ohci_open_endpoint(usb_device_t* d, const usb_ep_desc_t* ep) {
    ohci_t* o = (ohci_t*)d->hc->priv;
    odev_t* od = (odev_t*)d->hcpriv;
    int type = ep->bmAttributes & 3;
    if (type != USB_EP_BULK && type != USB_EP_INTERRUPT) return -1;
    int idx = (ep->bEndpointAddress & 0x0F) * 2 + ((ep->bEndpointAddress & 0x80) ? 1 : 0);
    if (od->ep[idx]) return 0;
    od->ep[idx] = ep_new(o, od, ep->bEndpointAddress, ep->wMaxPacketSize & 0x7FF, type);
    return od->ep[idx] ? 0 : -1;
}

static int ohci_submit(usb_xfer_t* x) {
    ohci_t* o = (ohci_t*)x->dev->hc->priv;
    odev_t* od = (odev_t*)x->dev->hcpriv;
    int in = (x->ep & 0x80) != 0;
    oep_t* e = od->ep[(x->ep & 0x0F) * 2 + (in ? 1 : 0)];
    if (!e || e->xfer || x->len > 4096u * MAX_TD) { x->status = USB_XFER_ERROR; return -1; }
    int n = x->len ? (int)((x->len + 4095) / 4096) : 1;
    e->tds = (otd_t*)dma16((uint32_t)n * sizeof(otd_t), &e->tds_raw);
    if (!e->tds) { x->status = USB_XFER_ERROR; return -1; }
    uint32_t base = phys(x->buf);
    for (int i = 0; i < n; i++) {
        uint32_t off = (uint32_t)i * 4096, l = x->len - off < 4096 ? x->len - off : 4096;
        if (!x->len) l = 0;
        e->start[i] = base + off;
        e->len[i] = l;
        /* (a TD of up to 4 KiB crosses at most one page boundary: allowed) */
        td_fill(&e->tds[i], (in ? TD_IN : TD_OUT) | TD_ROUND, base + off, l, 0);
    }
    for (int i = 0; i + 1 < n; i++) e->tds[i].next = phys(&e->tds[i + 1]);
    e->tds[n - 1].next = phys(e->idle);
    e->ntd = n;
    e->xfer = x;
    __asm__ volatile("" ::: "memory");
    e->ed->head = phys(&e->tds[0]) | (e->ed->head & ED_CARRY);   /* (the data toggle carries over) */
    if (e->type == USB_EP_BULK) wr(o, R_CMDSTATUS, CS_BLF);
    return 0;
}

static void ohci_poll(usb_hc_t* hc) {
    ohci_t* o = (ohci_t*)hc->priv;
    for (int p = 1; p <= o->ports; p++)
        if (rd(o, R_PORT(p)) & P_CSC) hc->port_change = 1;
    wr(o, R_INTSTATUS, rd(o, R_INTSTATUS));       /* (nothing is enabled: just keep it tidy) */
    for (int k = 0; k < MAX_ODEV; k++) {
        odev_t* od = o->devs[k];
        if (!od) continue;
        for (int i = 1; i < 32; i++) {
            oep_t* e = od->ep[i];
            if (!e || !e->xfer) continue;
            int status;
            uint32_t actual;
            int r = ep_check(e, 0, &status, &actual);
            if (!r) continue;
            int halted = (e->ed->head & ED_HALTED) != 0;
            if (r == 2 || halted) ep_reset_queue(e, halted);
            usb_xfer_t* x = e->xfer;
            e->xfer = NULL;
            ep_drop_tds(e);
            x->actual = actual;
            x->status = status;
        }
    }
}

/* ── devices ────────────────────────────────────────────────────── */

static odev_t* odev_new(ohci_t* o, uint8_t port, int low) {
    int slot = -1;
    for (int i = 0; i < MAX_ODEV; i++) if (!o->devs[i]) { slot = i; break; }
    if (slot < 0) return NULL;
    odev_t* od = (odev_t*)kzalloc(sizeof(odev_t));
    if (!od) return NULL;
    od->port = port;
    od->low = (uint8_t)low;
    od->ctrl_buf = (uint8_t*)usb_dma_alloc(8192);
    od->ep[0] = od->ctrl_buf ? ep_new(o, od, 0, 8, USB_EP_CONTROL) : NULL;
    if (!od->ep[0]) { kfree(od); return NULL; }
    o->devs[slot] = od;
    return od;
}

static int odev_address(ohci_t* o, odev_t* od) {
    uint8_t addr = o->next_addr++;
    if (o->next_addr > 127) o->next_addr = 1;
    usb_device_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.hc = &o->hc;
    tmp.hcpriv = od;
    tmp.present = 1;
    usb_setup_t sa = { USB_TYPE_STANDARD | USB_RECIP_DEVICE, USB_REQ_SET_ADDRESS, addr, 0, 0 };
    if (ohci_control(&tmp, &sa, NULL, 1000) < 0) return -1;
    usb_delay_ms(5);
    od->addr = addr;
    for (int i = 0; i < 32; i++) if (od->ep[i]) ep_set_addr(od->ep[i], od);
    return 0;
}

static void odev_free(ohci_t* o, odev_t* od) {
    if (!od) return;
    for (int i = 0; i < MAX_ODEV; i++) if (o->devs[i] == od) o->devs[i] = NULL;
    for (int i = 0; i < 32; i++) {
        oep_t* e = od->ep[i];
        if (!e) continue;
        ep_unlink(o, e);
        if (e->xfer) e->xfer->status = USB_XFER_GONE;
        ep_drop_tds(e);
        kfree(e->ed_raw);
        kfree(e->idle_raw);
        kfree(e);
    }
    kfree(od);
}

static void attach_port(ohci_t* o, int p) {
    wr(o, R_PORT(p), W_SET_RESET);
    uint32_t t0 = timer_ms();
    while (!(rd(o, R_PORT(p)) & P_PRSC) && timer_ms() - t0 < 100) usb_delay_ms(1);
    wr(o, R_PORT(p), P_PRSC | P_CSC | P_PESC);
    usb_delay_ms(10);                              /* reset recovery */
    uint32_t v = rd(o, R_PORT(p));
    if (!(v & P_PES) || !(v & P_CCS)) { klog("ohci: port %d does not enable\n", p); return; }
    int low = (v & P_LSDA) != 0;
    odev_t* od = odev_new(o, (uint8_t)p, low);
    if (!od) return;
    o->port_dev[p] = od;
    if (odev_address(o, od) < 0) {
        klog("ohci: port %d: SET_ADDRESS failed\n", p);
        o->port_dev[p] = NULL;
        odev_free(o, od);
        return;
    }
    od->udev = usb_new_device(&o->hc, p, low ? USB_SPEED_LOW : USB_SPEED_FULL, od->addr, 8, od);
}

static void detach_port(ohci_t* o, int p) {
    odev_t* od = o->port_dev[p];
    if (!od) return;
    o->port_dev[p] = NULL;
    if (od->udev) usb_device_gone(od->udev);
    odev_free(o, od);
}

static void ohci_rescan(usb_hc_t* hc) {
    ohci_t* o = (ohci_t*)hc->priv;
    for (int p = 1; p <= o->ports; p++) {
        uint32_t v = rd(o, R_PORT(p));
        int changed = (v & P_CSC) != 0;
        if (v & P_WC) wr(o, R_PORT(p), v & P_WC);
        int connected = (v & P_CCS) != 0;
        if (o->port_dev[p] && (!connected || changed)) detach_port(o, p);
        if (connected && !o->port_dev[p]) attach_port(o, p);
    }
}

static usb_device_t* ohci_attach_child(usb_hc_t* hc, usb_device_t* hub, int port, int speed) {
    ohci_t* o = (ohci_t*)hc->priv;
    if (speed == USB_SPEED_HIGH) speed = USB_SPEED_FULL;
    odev_t* od = odev_new(o, (uint8_t)port, speed == USB_SPEED_LOW);
    if (!od) return NULL;
    if (odev_address(o, od) < 0) { klog("ohci: hub port %d: SET_ADDRESS failed\n", port); odev_free(o, od); return NULL; }
    od->udev = usb_new_child(hc, hub, port, speed, od->addr, 8, od);
    if (!od->udev) { odev_free(o, od); return NULL; }
    return od->udev;
}

static void ohci_detach_child(usb_device_t* d) { odev_free((ohci_t*)d->hc->priv, (odev_t*)d->hcpriv); }

static const usb_hc_ops_t g_ops = {
    ohci_control, ohci_set_ep0_mps, ohci_open_endpoint, ohci_submit, ohci_poll, ohci_rescan,
    ohci_attach_child, ohci_detach_child,
};

/* ── bring-up ───────────────────────────────────────────────────── */

static oed_t* anchor_new(void) {
    uint8_t* raw;
    oed_t* a = (oed_t*)dma16(sizeof(oed_t), &raw);
    if (a) a->ctrl = ED_SKIP;                      /* never processed itself: just the list's start */
    return a;
}

int ohci_init_controller(const pci_dev_t* pd) {
    if (g_ohci_count >= MAX_OHCI) return -1;
    int is_io = 0;
    uintptr_t bar = pci_bar(pd, 0, &is_io);
    if (is_io || !bar) return -1;
    pci_enable(pd);
    ohci_t* o = (ohci_t*)kzalloc(sizeof(ohci_t));
    if (!o) return -1;
    o->mmio = (volatile uint8_t*)bar;
    o->next_addr = 1;
    uint32_t rev = rd(o, R_REVISION) & 0xFF;
    if (rev != 0x10 && rev != 0x11) klog("ohci: revision %x (expected 1.0)\n", rev);

    /* from the BIOS: SMM first (5.1.1.3.3), else a driver it left running */
    uint32_t c = rd(o, R_CONTROL);
    if (c & CTL_IR) {
        wr(o, R_CMDSTATUS, CS_OCR);
        uint32_t t0 = timer_ms();
        while ((rd(o, R_CONTROL) & CTL_IR) && timer_ms() - t0 < 1000) timer_idle();
        if (rd(o, R_CONTROL) & CTL_IR) klog("ohci: the BIOS did not hand it over\n");
    } else if ((c & CTL_HCFS) && (c & CTL_HCFS) != CTL_OPER) {
        wr(o, R_CONTROL, (c & ~CTL_HCFS) | CTL_RESUME);
        usb_delay_ms(20);
    }
    wr(o, R_INTDISABLE, 0xFFFFFFFFu);              /* polled: no interrupts */
    uint32_t fmi = rd(o, R_FMINTERVAL);
    wr(o, R_CMDSTATUS, CS_HCR);
    uint32_t t0 = timer_ms();
    while ((rd(o, R_CMDSTATUS) & CS_HCR) && timer_ms() - t0 < 50) timer_idle();
    if (rd(o, R_CMDSTATUS) & CS_HCR) { klog("ohci: reset failed\n"); kfree(o); return -1; }
    /* now in UsbSuspend: 2 ms to make it operational (5.1.1.4) */
    uint32_t fi = fmi & 0x3FFF;
    if (fi < 11000 || fi > 13000) fi = 11999;
    uint32_t fsmps = ((fi - 210) * 6 / 7) & 0x7FFF;

    o->hcca = (uint8_t*)usb_dma_alloc(256);        /* (4 KiB aligned: >= the 256 needed) */
    o->ctrl_anchor = anchor_new();
    o->bulk_anchor = anchor_new();
    o->intr_anchor = anchor_new();
    if (!o->hcca || !o->ctrl_anchor || !o->bulk_anchor || !o->intr_anchor) { kfree(o); return -1; }
    /* every slot of the interrupt table: the one interrupt anchor */
    for (int i = 0; i < 32; i++) ((volatile uint32_t*)o->hcca)[i] = phys(o->intr_anchor);
    wr(o, R_HCCA, phys(o->hcca));
    wr(o, R_CTRLHEAD, phys(o->ctrl_anchor));
    wr(o, R_CTRLCUR, 0);
    wr(o, R_BULKHEAD, phys(o->bulk_anchor));
    wr(o, R_BULKCUR, 0);
    wr(o, R_FMINTERVAL, ((fmi & 0x80000000u) ^ 0x80000000u) | (fsmps << 16) | fi);
    wr(o, R_PERSTART, fi * 9 / 10);
    wr(o, R_LSTHRESH, 0x628);
    wr(o, R_CONTROL, CTL_OPER | CTL_CBSR3 | CTL_PLE | CTL_CLE | CTL_BLE);
    usb_delay_ms(5);

    /* the root hub's ports: power on (globally and per port) */
    uint32_t da = rd(o, R_RHDESCA);
    o->ports = (int)(da & 0xFF);
    if (o->ports > 15) o->ports = 15;
    if (o->ports < 1) o->ports = 1;
    wr(o, R_RHSTATUS, 1u << 16);                   /* SetGlobalPower */
    for (int p = 1; p <= o->ports; p++) wr(o, R_PORT(p), W_SET_POWER);
    uint32_t potpgt = (da >> 24) & 0xFF;
    usb_delay_ms(potpgt * 2 + 20);

    o->hc.name = "ohci";
    o->hc.ops = &g_ops;
    o->hc.priv = o;
    o->hc.ports = o->ports;
    ksnprintf(o->hc.desc, sizeof(o->hc.desc), "OHCI (USB 1.1), %d ports (PCI %02x:%02x.%x, %04x:%04x)",
              o->ports, pd->bus, pd->dev, pd->fn, pd->vendor, pd->device);
    g_ohci[g_ohci_count++] = o;
    usb_register_hc(&o->hc);
    klog("ohci: %s\n", o->hc.desc);
    return 0;
}
