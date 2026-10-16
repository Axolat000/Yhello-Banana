#include "virtio_pci.h"
#include "io.h"
#include "kstring.h"

/* legacy registers (I/O space) */
#define L_DEV_FEATURES   0x00
#define L_GUEST_FEATURES 0x04
#define L_QUEUE_PFN      0x08
#define L_QUEUE_SIZE     0x0C
#define L_QUEUE_SEL      0x0E
#define L_QUEUE_NOTIFY   0x10
#define L_STATUS         0x12
#define L_ISR            0x13
#define L_DEVICE         0x14           /* the device configuration (MSI-X off) */

/* modern: the capabilities, and the common configuration */
#define CAP_VENDOR   0x09
#define CFG_COMMON   1
#define CFG_NOTIFY   2
#define CFG_ISR      3
#define CFG_DEVICE   4

#define C_DFSELECT   0x00
#define C_DF         0x04
#define C_GFSELECT   0x08
#define C_GF         0x0C
#define C_STATUS     0x14
#define C_QSELECT    0x16
#define C_QSIZE      0x18
#define C_QENABLE    0x1C
#define C_QNOTIFYOFF 0x1E
#define C_QDESC      0x20
#define C_QDRIVER    0x28
#define C_QDEVICE    0x30

static uint16_t c16(vio_t* v, int o) { return *(volatile uint16_t*)(v->common + o); }
static uint32_t c32(vio_t* v, int o) { return *(volatile uint32_t*)(v->common + o); }
static void w8(vio_t* v, int o, uint8_t x) { v->common[o] = x; }
static void w16(vio_t* v, int o, uint16_t x) { *(volatile uint16_t*)(v->common + o) = x; }
static void w32(vio_t* v, int o, uint32_t x) { *(volatile uint32_t*)(v->common + o) = x; }
static void w64(vio_t* v, int o, uint64_t x) { w32(v, o, (uint32_t)x); w32(v, o + 4, (uint32_t)(x >> 32)); }

static uint8_t status(vio_t* v) { return v->modern ? v->common[C_STATUS] : inb((uint16_t)(v->io + L_STATUS)); }
static void set_status(vio_t* v, uint8_t s) {
    if (v->modern) w8(v, C_STATUS, s);
    else outb((uint16_t)(v->io + L_STATUS), s);
}

static int find_caps(vio_t* v, const pci_dev_t* d) {
    if (!(pci_read16(d->bus, d->dev, d->fn, 0x06) & 0x10)) return -1;     /* no capability list */
    uint8_t p = (uint8_t)(pci_read32(d->bus, d->dev, d->fn, 0x34) & 0xFC);
    for (int guard = 0; p && guard < 48; guard++) {
        uint32_t h = pci_read32(d->bus, d->dev, d->fn, p);
        uint8_t next = (uint8_t)(h >> 8);
        if ((h & 0xFF) == CAP_VENDOR) {
            uint8_t type = (uint8_t)(h >> 24);
            uint8_t bar = (uint8_t)pci_read32(d->bus, d->dev, d->fn, (uint8_t)(p + 4));
            uint32_t off = pci_read32(d->bus, d->dev, d->fn, (uint8_t)(p + 8));
            if (bar < 6 && type >= CFG_COMMON && type <= CFG_DEVICE) {
                int io = 0;
                uintptr_t base = pci_bar(d, bar, &io);
                if (base && !io) {
                    volatile uint8_t* at = (volatile uint8_t*)(base + off);
                    if (type == CFG_COMMON && !v->common) v->common = at;
                    else if (type == CFG_NOTIFY && !v->notify) {
                        v->notify = at;
                        v->notify_mul = pci_read32(d->bus, d->dev, d->fn, (uint8_t)(p + 16));
                    } else if (type == CFG_ISR && !v->isr) v->isr = at;
                    else if (type == CFG_DEVICE && !v->device) v->device = at;
                }
            }
        }
        p = next & 0xFC;
    }
    return v->common && v->notify && v->isr ? 0 : -1;
}

int vio_open(vio_t* v, const pci_dev_t* d) {
    memset(v, 0, sizeof(*v));
    pci_enable(d);
    if (find_caps(v, d) == 0) v->modern = 1;
    else {
        memset(v, 0, sizeof(*v));
        int io = 0;
        uintptr_t bar0 = pci_bar(d, 0, &io);
        if (!io || !bar0) return -1;
        v->io = (uint16_t)bar0;
    }
    set_status(v, 0);                                  /* reset */
    for (int i = 0; i < 1000000 && status(v); i++) __asm__ volatile("pause");
    set_status(v, VIO_S_ACK);
    set_status(v, VIO_S_ACK | VIO_S_DRIVER);
    return 0;
}

uint32_t vio_features(vio_t* v) {
    if (!v->modern) return inl((uint16_t)(v->io + L_DEV_FEATURES));
    w32(v, C_DFSELECT, 0);
    return c32(v, C_DF);
}

int vio_set_features(vio_t* v, uint32_t want) {
    if (!v->modern) {
        outl((uint16_t)(v->io + L_GUEST_FEATURES), want);
        return 0;
    }
    w32(v, C_DFSELECT, 1);
    if (!(c32(v, C_DF) & 1)) return -1;                /* (no VIRTIO_F_VERSION_1?) */
    w32(v, C_GFSELECT, 0);
    w32(v, C_GF, want);
    w32(v, C_GFSELECT, 1);
    w32(v, C_GF, 1);                                   /* VERSION_1 */
    set_status(v, VIO_S_ACK | VIO_S_DRIVER | VIO_S_FEATURES_OK);
    return (status(v) & VIO_S_FEATURES_OK) ? 0 : -1;
}

uint16_t vio_queue(vio_t* v, int qi, uint8_t* mem, uint16_t max,
                   vio_desc_t** desc, volatile uint16_t** avail, volatile uint8_t** used) {
    uint16_t size;
    if (max > VIO_QMAX) max = VIO_QMAX;
    if (v->modern) {
        w16(v, C_QSELECT, (uint16_t)qi);
        size = c16(v, C_QSIZE);
        if (!size) return 0;
        if (size > max) { size = max; w16(v, C_QSIZE, size); }     /* (modern queues may be made smaller) */
    } else {
        outw((uint16_t)(v->io + L_QUEUE_SEL), (uint16_t)qi);
        size = inw((uint16_t)(v->io + L_QUEUE_SIZE));
        if (!size || size > VIO_QMAX) return 0;                    /* (legacy: the device's size or nothing) */
    }
    memset(mem, 0, VIO_QBYTES);
    *desc = (vio_desc_t*)mem;
    *avail = (volatile uint16_t*)(mem + 16u * size);
    *used = mem + ((16u * size + 6u + 2u * size + 4095u) & ~4095u);
    if (v->modern) {
        w64(v, C_QDESC, (uintptr_t)*desc);
        w64(v, C_QDRIVER, (uintptr_t)*avail);
        w64(v, C_QDEVICE, (uintptr_t)*used);
        if (qi < 32) v->notify_off[qi] = c16(v, C_QNOTIFYOFF);
        w16(v, C_QENABLE, 1);
    } else {
        outl((uint16_t)(v->io + L_QUEUE_PFN), (uint32_t)((uintptr_t)mem >> 12));
    }
    return size;
}

void vio_driver_ok(vio_t* v) {
    set_status(v, (uint8_t)(VIO_S_ACK | VIO_S_DRIVER | VIO_S_DRIVER_OK | (v->modern ? VIO_S_FEATURES_OK : 0)));
}

void vio_failed(vio_t* v) { set_status(v, VIO_S_FAILED); }

void vio_notify(vio_t* v, int qi) {
    __asm__ volatile("" ::: "memory");
    if (v->modern) *(volatile uint16_t*)(v->notify + (uint32_t)v->notify_off[qi & 31] * v->notify_mul) = (uint16_t)qi;
    else outw((uint16_t)(v->io + L_QUEUE_NOTIFY), (uint16_t)qi);
}

uint8_t vio_isr(vio_t* v) { return v->modern ? *v->isr : inb((uint16_t)(v->io + L_ISR)); }

uint8_t vio_cfg8(vio_t* v, int off) {
    if (v->modern) return v->device ? v->device[off] : 0;
    return inb((uint16_t)(v->io + L_DEVICE + off));
}

uint32_t vio_cfg32(vio_t* v, int off) {
    if (v->modern) return v->device ? *(volatile uint32_t*)(v->device + off) : 0;
    return inl((uint16_t)(v->io + L_DEVICE + off));
}
