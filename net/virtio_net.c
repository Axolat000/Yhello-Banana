#include "netdev.h"
#include "pci.h"
#include "idt.h"
#include "../kernel/virtio_pci.h"
#include "kstring.h"
#include "serial.h"

/*
 * virtio-net driver: VirtualBox's "Paravirtualized Network (virtio-net)",
 * QEMU's -nic user,model=virtio-net-pci, cloud VMs - legacy or modern
 * (kernel/virtio_pci.c).
 *
 * Frames go through
 * two virtqueues - 0 receive, 1 transmit - rings in memory shared with the
 * device: a descriptor table (buffer address + length), the "available"
 * ring (buffers handed to the device) and the "used" ring (buffers it is
 * done with). Every frame is preceded by a virtio-net header (10 bytes,
 * 12 on a modern device).
 */

#define F_NET_MAC   (1u << 5)

#define D_WRITE 2                     /* the device writes into this buffer */

#define HDR_MAX  12
#define BUF_SIZE (HDR_MAX + 1514 + 2)
#define RX_BUFS  64
#define TX_BUFS  16

typedef struct __attribute__((packed)) { uint32_t id, len; } vused_elem_t;

typedef struct {
    vio_desc_t*         desc;
    volatile uint16_t*  avail;        /* flags, idx, ring[size], used_event */
    volatile uint8_t*   used;         /* flags, idx, ring[size] of vused_elem_t */
    uint16_t            size, last_used, avail_idx;
} vq_t;

/* (the kernel image is identity mapped below 4 GB: these addresses are physical) */
static uint8_t g_ring[2][VIO_QBYTES] __attribute__((aligned(4096)));
static uint8_t g_rxbuf[RX_BUFS][BUF_SIZE] __attribute__((aligned(16)));
static uint8_t g_txbuf[TX_BUFS][BUF_SIZE] __attribute__((aligned(16)));

static vio_t    g_vio;
static uint32_t HDR = 10;              /* struct virtio_net_hdr (no merged buffers) */
static vq_t     g_q[2];
static int      g_tx_next;
static netdev_t g_nd;

static uint16_t used_idx(vq_t* q) { return *(volatile uint16_t*)(q->used + 2); }
static vused_elem_t used_elem(vq_t* q, uint16_t i) {
    return *(volatile vused_elem_t*)(q->used + 4 + (uint32_t)(i % q->size) * 8u);
}

/* sets up virtqueue n in its static ring; 0 if the device's size is unusable */
static int setup_queue(int n) {
    vq_t* q = &g_q[n];
    q->size = vio_queue(&g_vio, n, g_ring[n], 256, &q->desc, &q->avail, &q->used);
    q->last_used = q->avail_idx = 0;
    return q->size != 0;
}

/* hands descriptor d to the device (into the available ring) */
static void offer(vq_t* q, uint16_t d) {
    q->avail[2 + (q->avail_idx % q->size)] = d;
    __asm__ volatile("" ::: "memory");
    q->avail_idx++;
    q->avail[1] = q->avail_idx;
}

static void vio_irq(void) {
    uint8_t isr = vio_isr(&g_vio);                      /* reading it clears it */
    if (isr & 1) net_irq_notify();
}

static int vio_send(netdev_t* nd, const void* frame, uint32_t len) {
    if (len > ETH_FRAME_MAX) { nd->tx_errors++; return -1; }
    vq_t* q = &g_q[1];
    /* frames the device has sent free their buffers */
    for (int i = 0; (uint16_t)(q->avail_idx - used_idx(q)) >= TX_BUFS; i++) {
        if (i > 2000000) { nd->tx_errors++; return -1; }
    }
    q->last_used = used_idx(q);
    int slot = g_tx_next;
    g_tx_next = (g_tx_next + 1) % TX_BUFS;
    memset(g_txbuf[slot], 0, HDR);                       /* no checksum offload, no GSO */
    memcpy(g_txbuf[slot] + HDR, frame, len);
    q->desc[slot].addr = (uint64_t)(uintptr_t)g_txbuf[slot];
    q->desc[slot].len = HDR + len;
    q->desc[slot].flags = 0;
    q->desc[slot].next = 0;
    offer(q, (uint16_t)slot);
    __asm__ volatile("" ::: "memory");
    vio_notify(&g_vio, 1);
    nd->tx_packets++;
    nd->tx_bytes += len;
    return 0;
}

static int vio_poll(netdev_t* nd) {
    vq_t* q = &g_q[0];
    int count = 0;
    while (q->last_used != used_idx(q)) {
        vused_elem_t e = used_elem(q, q->last_used);
        q->last_used++;
        if (e.id < RX_BUFS && e.len > HDR && e.len <= BUF_SIZE) {
            nd->rx_packets++;
            nd->rx_bytes += e.len - HDR;
            net_rx(nd, g_rxbuf[e.id] + HDR, e.len - HDR);
        } else {
            nd->rx_errors++;
        }
        if (e.id < RX_BUFS) offer(q, (uint16_t)e.id);    /* the buffer goes back to the device */
        if (++count >= 64) break;
    }
    if (count) vio_notify(&g_vio, 0);
    return count;
}

static int vio_link_up(netdev_t* nd) { (void)nd; return 1; }

netdev_t* virtio_net_probe(void) {
    pci_dev_t pd;
    if (!pci_find(0x1AF4, 0x1000, &pd) && !pci_find(0x1AF4, 0x1041, &pd)) return NULL;
    if (vio_open(&g_vio, &pd) != 0) return NULL;
    uint32_t feat = vio_features(&g_vio);
    if (vio_set_features(&g_vio, feat & F_NET_MAC) != 0) { vio_failed(&g_vio); return NULL; }
    HDR = g_vio.modern ? 12 : 10;

    memset(&g_nd, 0, sizeof(g_nd));
    if (feat & F_NET_MAC) {
        for (int i = 0; i < 6; i++) g_nd.mac[i] = vio_cfg8(&g_vio, i);
    } else {
        static const uint8_t def[6] = { 0x02, 0x42, 0x41, 0x4E, 0x41, 0x4E };   /* locally administered */
        memcpy(g_nd.mac, def, 6);
    }

    if (!setup_queue(0) || !setup_queue(1)) {
        vio_failed(&g_vio);
        klog("virtio-net: unusable queue size\n");
        return NULL;
    }
    /* every receive buffer to the device */
    vq_t* rq = &g_q[0];
    int nrx = RX_BUFS < rq->size ? RX_BUFS : rq->size;
    for (int i = 0; i < nrx; i++) {
        rq->desc[i].addr = (uint64_t)(uintptr_t)g_rxbuf[i];
        rq->desc[i].len = BUF_SIZE;
        rq->desc[i].flags = D_WRITE;
        offer(rq, (uint16_t)i);
    }
    g_tx_next = 0;
    vio_driver_ok(&g_vio);
    vio_notify(&g_vio, 0);

    g_nd.name = "virtio-net";
    g_nd.model = "virtio-net (paravirtualized)";
    g_nd.send = vio_send;
    g_nd.poll = vio_poll;
    g_nd.link_up = vio_link_up;
    g_nd.irq = pd.irq_line;
    if (pd.irq_line > 0 && pd.irq_line < 16) irq_install(pd.irq_line, vio_irq);
    else g_nd.irq = 0xFF;

    klog("virtio-net: %s irq %u queues %u/%u mac %02x:%02x:%02x:%02x:%02x:%02x\n",
         g_vio.modern ? "modern" : "legacy", pd.irq_line, g_q[0].size, g_q[1].size, g_nd.mac[0], g_nd.mac[1], g_nd.mac[2],
         g_nd.mac[3], g_nd.mac[4], g_nd.mac[5]);
    return &g_nd;
}
