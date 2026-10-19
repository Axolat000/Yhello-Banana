/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "virtio_serial.h"
#include "virtio_pci.h"
#include "pci.h"
#include "kheap.h"
#include "kstring.h"
#include "task.h"
#include "timer.h"
#include "serial.h"

/*
 * virtio-serial (virtio 1.x spec, 5.3 "Console Device"): with MULTIPORT the
 * queues are port 0 rx/tx (0, 1), the control rx/tx (2, 3), then port p's
 * rx/tx at 2p + 2 / 2p + 3. The host announces each port (DEVICE_ADD), its
 * name (PORT_NAME) and whether something is connected on its side
 * (PORT_OPEN); the guest says it is ready (DEVICE_READY, PORT_READY) and
 * when it opens a port (PORT_OPEN). Ports beyond the first 8 are not set
 * up: the agents QEMU and Proxmox add get the first numbers.
 */

#define F_MULTIPORT (1u << 1)

#define EV_DEVICE_READY  0
#define EV_DEVICE_ADD    1
#define EV_DEVICE_REMOVE 2
#define EV_PORT_READY    3
#define EV_CONSOLE_PORT  4
#define EV_RESIZE        5
#define EV_PORT_OPEN     6
#define EV_PORT_NAME     7

#define D_WRITE 2

#define SETUP_PORTS 8                   /* ports whose queues are made (16 + 2 queues) */
#define RX_BUFS     16
#define RX_SIZE     4096u               /* a port's receive buffers (the control queue's too) */
#define TX_SIZE     4096u

typedef struct __attribute__((packed)) { uint32_t id; uint16_t event, value; } ctrl_t;

typedef struct {
    uint8_t* ring;
    vio_desc_t* desc;
    volatile uint16_t* avail;
    volatile uint8_t* used;
    uint16_t size, last_used, avail_idx;
    uint8_t* bufs;                      /* rx: RX_BUFS * RX_SIZE; tx: one TX_SIZE buffer */
    int      ok;
} vq_t;

typedef struct {
    int       added, host_open, guest_open, console;
    char      name[64];
    vser_rx_t rx;
    void*     ctx;
    uint32_t  rx_bytes, tx_bytes;
} port_t;

static vio_t    g_vio;
static int      g_present, g_multiport;
static uint32_t g_nports;
static vq_t     g_q[2 * SETUP_PORTS + 2];
static port_t   g_port[SETUP_PORTS];
static volatile int g_ctl_lock;

static void* page_alloc(uint32_t size) {
    uint8_t* raw = (uint8_t*)kmalloc(size + 4096);
    if (!raw) return NULL;
    uint8_t* p = (uint8_t*)(((uintptr_t)raw + 4095) & ~(uintptr_t)4095);
    memset(p, 0, size);
    return p;
}

static int rxq(int port) { return port == 0 ? 0 : 2 * port + 2; }
static int txq(int port) { return port == 0 ? 1 : 2 * port + 3; }
#define CRXQ 2
#define CTXQ 3

static uint16_t used_idx(vq_t* q) { return *(volatile uint16_t*)(q->used + 2); }

/* buffer id into the available ring */
static void give(int qi, uint16_t id) {
    vq_t* q = &g_q[qi];
    q->avail[2 + q->avail_idx % q->size] = id;
    __asm__ volatile("" ::: "memory");
    q->avail_idx++;
    q->avail[1] = q->avail_idx;
    vio_notify(&g_vio, qi);
}

/* every receive buffer of queue qi given to the device */
static int rx_post_all(int qi) {
    vq_t* q = &g_q[qi];
    if (!q->ok) return -1;
    if (!q->bufs && !(q->bufs = (uint8_t*)kmalloc(RX_BUFS * RX_SIZE))) return -1;
    uint16_t n = q->size < RX_BUFS ? q->size : RX_BUFS;
    for (uint16_t i = 0; i < n; i++) {
        q->desc[i].addr = (uint64_t)(uintptr_t)(q->bufs + (uint32_t)i * RX_SIZE);
        q->desc[i].len = RX_SIZE;
        q->desc[i].flags = D_WRITE;
        q->desc[i].next = 0;
        q->avail[2 + q->avail_idx % q->size] = i;
        q->avail_idx++;
    }
    __asm__ volatile("" ::: "memory");
    q->avail[1] = q->avail_idx;
    vio_notify(&g_vio, qi);
    return 0;
}

/* one buffer out on queue qi, waited for (up to 1 s): 0, or -1 */
static int tx_send(int qi, const void* data, uint32_t len) {
    vq_t* q = &g_q[qi];
    if (!q->ok || len > TX_SIZE) return -1;
    if (!q->bufs && !(q->bufs = (uint8_t*)kmalloc(TX_SIZE))) return -1;
    memcpy(q->bufs, data, len);
    q->desc[0].addr = (uint64_t)(uintptr_t)q->bufs;
    q->desc[0].len = len;
    q->desc[0].flags = 0;
    q->desc[0].next = 0;
    uint16_t want = (uint16_t)(q->last_used + 1);
    give(qi, 0);
    uint32_t t0 = timer_ms();
    while (used_idx(q) != want) {
        if (timer_ms() - t0 > 1000u) {               /* the host is not taking it: drop the queue's state */
            q->last_used = used_idx(q);
            return -1;
        }
        task_sleep_ms(1);
    }
    q->last_used = want;
    return 0;
}

static void ctl_send(uint32_t id, uint16_t event, uint16_t value) {
    while (g_ctl_lock) task_sleep_ms(1);
    g_ctl_lock = 1;
    ctrl_t c = { id, event, value };
    tx_send(CTXQ, &c, sizeof(c));
    g_ctl_lock = 0;
}

static void on_control(const uint8_t* b, uint32_t len) {
    if (len < sizeof(ctrl_t)) return;
    ctrl_t c;
    memcpy(&c, b, sizeof(c));
    if (c.id >= SETUP_PORTS && c.event != EV_RESIZE) return;
    port_t* p = &g_port[c.id < SETUP_PORTS ? c.id : 0];
    switch (c.event) {
    case EV_DEVICE_ADD:
        p->added = 1;
        rx_post_all(rxq((int)c.id));
        ctl_send(c.id, EV_PORT_READY, 1);
        break;
    case EV_DEVICE_REMOVE:
        memset(p, 0, sizeof(*p));
        break;
    case EV_PORT_NAME: {
        uint32_t n = len - (uint32_t)sizeof(ctrl_t);
        if (n >= sizeof(p->name)) n = sizeof(p->name) - 1;
        memcpy(p->name, b + sizeof(ctrl_t), n);
        p->name[n] = 0;
        while (n && (p->name[n - 1] == 0 || p->name[n - 1] == '\n')) p->name[--n] = 0;
        klog("virtio-serial: port %u is %s\n", c.id, p->name);
        break;
    }
    case EV_PORT_OPEN:
        p->host_open = c.value ? 1 : 0;
        break;
    case EV_CONSOLE_PORT:
        p->console = 1;
        ctl_send(c.id, EV_PORT_OPEN, 1);
        p->guest_open = 1;
        break;
    default:
        break;
    }
}

/* what came in on queue qi: handled, its buffers given back */
static void drain(int qi, int port) {
    vq_t* q = &g_q[qi];
    if (!q->ok || !q->bufs) return;
    while (q->last_used != used_idx(q)) {
        const volatile uint8_t* e = q->used + 4 + 8u * (q->last_used % q->size);
        uint32_t id = *(const volatile uint32_t*)e, len = *(const volatile uint32_t*)(e + 4);
        q->last_used++;
        if (id >= RX_BUFS) continue;
        if (len > RX_SIZE) len = RX_SIZE;
        const uint8_t* b = q->bufs + id * RX_SIZE;
        if (port < 0) on_control(b, len);
        else {
            port_t* p = &g_port[port];
            p->rx_bytes += len;
            if (p->rx) p->rx(port, b, len, p->ctx);
        }
        give(qi, (uint16_t)id);
    }
}

static void vser_task(void) {
    task_set_background();
    for (;;) {
        drain(CRXQ, -1);
        for (int p = 0; p < (int)g_nports; p++) if (g_port[p].added) drain(rxq(p), p);
        task_sleep_ms(20);
    }
}

static int setup(const pci_dev_t* d) {
    pci_enable(d);
    if (vio_open(&g_vio, d) != 0) return -1;
    uint32_t f = vio_features(&g_vio);
    g_multiport = (f & F_MULTIPORT) ? 1 : 0;
    if (!g_multiport) {
        klog("virtio-serial: no MULTIPORT - one unnamed console port, not used\n");
        vio_failed(&g_vio);
        return -1;
    }
    if (vio_set_features(&g_vio, F_MULTIPORT) != 0) { vio_failed(&g_vio); return -1; }
    g_nports = vio_cfg32(&g_vio, 4);
    if (g_nports > SETUP_PORTS) g_nports = SETUP_PORTS;
    if (!g_nports) g_nports = 1;
    int nq = 2 * (int)g_nports + 2;
    for (int qi = 0; qi < nq; qi++) {
        vq_t* q = &g_q[qi];
        q->ring = (uint8_t*)page_alloc(VIO_QBYTES);
        if (!q->ring) return -1;
        q->size = vio_queue(&g_vio, qi, q->ring, 64, &q->desc, &q->avail, &q->used);
        q->ok = q->size != 0;
    }
    if (!g_q[CRXQ].ok || !g_q[CTXQ].ok) { vio_failed(&g_vio); return -1; }
    vio_driver_ok(&g_vio);
    rx_post_all(CRXQ);
    ctl_send(0, EV_DEVICE_READY, 1);
    return 0;
}

int vser_init(void) {
    pci_dev_t d;
    if (!pci_find(0x1AF4, 0x1003, &d) && !pci_find(0x1AF4, 0x1043, &d)) return 0;
    if (setup(&d) != 0) { klog("virtio-serial: the device could not be set up\n"); return 0; }
    g_present = 1;
    klog("virtio-serial: %s interface, %u ports\n", g_vio.modern ? "modern" : "legacy", g_nports);
    task_create("virtio-serial", vser_task);
    return 1;
}

int vser_present(void) { return g_present; }

int vser_find(const char* name) {
    for (int p = 0; p < (int)g_nports; p++)
        if (g_port[p].added && strcmp(g_port[p].name, name) == 0) return p;
    return -1;
}

int vser_open(int port, vser_rx_t rx, void* ctx) {
    if (!g_present || port < 0 || port >= (int)g_nports || !g_port[port].added) return -1;
    g_port[port].rx = rx;
    g_port[port].ctx = ctx;
    if (!g_port[port].guest_open) {
        ctl_send((uint32_t)port, EV_PORT_OPEN, 1);
        g_port[port].guest_open = 1;
    }
    return 0;
}

int vser_write(int port, const void* data, uint32_t len) {
    if (!g_present || port < 0 || port >= (int)g_nports || !g_port[port].added) return -1;
    const uint8_t* p = (const uint8_t*)data;
    while (len) {
        uint32_t n = len < TX_SIZE ? len : TX_SIZE;
        if (tx_send(txq(port), p, n) != 0) return -1;
        g_port[port].tx_bytes += n;
        p += n;
        len -= n;
    }
    return 0;
}

int vser_host_connected(int port) {
    return port >= 0 && port < (int)g_nports && g_port[port].host_open;
}

void vser_describe(void (*out)(const char* line)) {
    char b[160];
    if (!g_present) { out("virtio-serial: none"); return; }
    ksnprintf(b, sizeof(b), "virtio-serial: %s, %u ports set up", g_vio.modern ? "modern" : "legacy", g_nports);
    out(b);
    for (int p = 0; p < (int)g_nports; p++) {
        port_t* q = &g_port[p];
        if (!q->added) continue;
        ksnprintf(b, sizeof(b), "  port %d: %s%s%s - %u bytes in, %u out", p, q->name[0] ? q->name : "(no name)",
                  q->host_open ? " (host connected)" : "", q->console ? " (console)" : "", q->rx_bytes, q->tx_bytes);
        out(b);
    }
}
