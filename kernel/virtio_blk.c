#include "virtio_blk.h"
#include "iowait.h"
#include "idt.h"
#include "pci.h"
#include "virtio_pci.h"
#include "kheap.h"
#include "kstring.h"
#include "timer.h"
#include "serial.h"
#include "blockdev.h"

/*
 * virtio-blk: the paravirtualized disks of QEMU / KVM, Proxmox and most
 * cloud VMs (-drive if=virtio, -device virtio-blk-pci), legacy or modern
 * (kernel/virtio_pci.c).
 *
 * One virtqueue of requests; each is three descriptors - the request
 * header (read / write / flush and the sector), the data, and one status
 * byte the device writes. Requests are made one at a time and waited for
 * (interrupt-driven where the line is usable, else polled - kernel/iowait.c),
 * through a 64 KiB bounce buffer as for NVMe. FAT32 partitions on
 * these disks are mounted under /mnt/virtio; Banana OS installs on them too.
 */

#define F_RO    (1u << 5)
#define F_FLUSH (1u << 9)

#define T_IN    0
#define T_OUT   1
#define T_FLUSH 4

#define D_NEXT  1
#define D_WRITE 2

#define CHUNK  65536u

typedef struct __attribute__((packed)) { uint32_t type, reserved; uint64_t sector; } vreq_t;

typedef struct {
    vio_t    vio;
    pci_dev_t pci;
    uint8_t* ring;                /* VIO_QBYTES, page aligned */
    vio_desc_t* desc;
    volatile uint16_t* avail;
    volatile uint8_t* used;
    uint16_t size, last_used, avail_idx;
    vreq_t*  req;
    volatile uint8_t* status;
    uint8_t* bounce;
    uint32_t sectors;             /* (the 32-bit API: up to 2 TB) */
    int      ro, flush;
    char     model[41];
    blockdev_t* bd;
    iowait_t w;                   /* one request at a time (one bounce buffer); who waits for it */
    int      irq;                 /* it interrupts when a request is done */
} vblk_t;

static vblk_t g_vb[VBLK_MAX];
static int    g_count;

static void* page_alloc(uint32_t size) {
    uint8_t* raw = (uint8_t*)kmalloc(size + 4096);
    if (!raw) return NULL;
    uint8_t* p = (uint8_t*)(((uintptr_t)raw + 4095) & ~(uintptr_t)4095);
    memset(p, 0, size);
    return p;
}

static uint16_t used_idx(vblk_t* v) { return *(volatile uint16_t*)(v->used + 2); }

static int req_done(void* ctx) { vblk_t* v = (vblk_t*)ctx; return used_idx(v) != v->last_used; }

/* a request finished on some disk (the line may be shared: reading the ISR
 * of one that did not interrupt gives 0) */
static void vblk_irq(void) {
    for (int i = 0; i < g_count; i++) {
        vblk_t* v = &g_vb[i];
        if (v->irq && (vio_isr(&v->vio) & 1)) io_wake(&v->w);
    }
}

/* one request: type, sector, count sectors through the bounce buffer; 0 = ok */
static int request(vblk_t* v, uint32_t type, uint32_t lba, uint32_t count) {
    v->req->type = type;
    v->req->reserved = 0;
    v->req->sector = lba;
    *v->status = 0xFF;
    v->desc[0].addr = (uint64_t)(uintptr_t)v->req;
    v->desc[0].len = sizeof(vreq_t);
    v->desc[0].flags = D_NEXT;
    v->desc[0].next = 1;
    uint16_t sd = 1;
    if (type != T_FLUSH) {
        v->desc[1].addr = (uint64_t)(uintptr_t)v->bounce;
        v->desc[1].len = count * 512;
        v->desc[1].flags = (uint16_t)(D_NEXT | (type == T_IN ? D_WRITE : 0));
        v->desc[1].next = 2;
        sd = 2;
    }
    v->desc[sd].addr = (uint64_t)(uintptr_t)v->status;
    v->desc[sd].len = 1;
    v->desc[sd].flags = D_WRITE;
    v->desc[sd].next = 0;
    v->avail[2 + (v->avail_idx % v->size)] = 0;
    __asm__ volatile("" ::: "memory");
    v->avail_idx++;
    v->avail[1] = v->avail_idx;
    __asm__ volatile("" ::: "memory");
    vio_notify(&v->vio, 0);
    /* the task sleeps until the device's interrupt (kernel/iowait.c) */
    if (io_wait(&v->w, req_done, v, 10000, v->irq) != 0) { klog("virtio-blk: request timed out\n"); return -1; }
    v->last_used = used_idx(v);
    (void)vio_isr(&v->vio);          /* (acknowledge, should it have raised one) */
    return *v->status == 0 ? 0 : -1;
}

int virtio_blk_read(int idx, uint32_t lba, uint32_t count, void* buf) {
    if (idx < 0 || idx >= g_count) return -1;
    vblk_t* v = &g_vb[idx];
    uint8_t* out = (uint8_t*)buf;
    int rc = 0;
    io_lock(&v->w);
    while (count) {
        uint32_t c = count > CHUNK / 512 ? CHUNK / 512 : count;
        if (request(v, T_IN, lba, c) != 0) { klog("virtio-blk: read error at %u\n", lba); rc = -1; break; }
        memcpy(out, v->bounce, c * 512);
        out += c * 512;
        lba += c;
        count -= c;
    }
    io_unlock(&v->w);
    return rc;
}

int virtio_blk_write(int idx, uint32_t lba, uint32_t count, const void* buf) {
    if (idx < 0 || idx >= g_count) return -1;
    vblk_t* v = &g_vb[idx];
    if (v->ro) return -1;
    const uint8_t* in = (const uint8_t*)buf;
    int rc = 0;
    io_lock(&v->w);
    while (count) {
        uint32_t c = count > CHUNK / 512 ? CHUNK / 512 : count;
        memcpy(v->bounce, in, c * 512);
        if (request(v, T_OUT, lba, c) != 0) { klog("virtio-blk: write error at %u\n", lba); rc = -1; break; }
        in += c * 512;
        lba += c;
        count -= c;
    }
    io_unlock(&v->w);
    return rc;
}

int virtio_blk_flush(int idx) {
    if (idx < 0 || idx >= g_count) return -1;
    vblk_t* v = &g_vb[idx];
    if (!v->flush) return 0;                          /* (writes go straight through) */
    io_lock(&v->w);
    int rc = request(v, T_FLUSH, 0, 0);
    io_unlock(&v->w);
    return rc;
}

int virtio_blk_probe(ata_disk_t* out, int max) {
    int n = 0;
    for (int i = 0; i < g_count && n < max; i++) {
        ata_disk_t* d = &out[n++];
        memset(d, 0, sizeof(*d));
        d->present = 1;
        d->ahci_port = -1;
        d->vblk = i + 1;
        d->sectors = g_vb[i].sectors;
        kstrlcpy(d->model, g_vb[i].model, sizeof(d->model));
    }
    return n;
}

static int bd_read(blockdev_t* bd, uint32_t lba, uint32_t count, void* buf) {
    return virtio_blk_read((int)(uintptr_t)bd->priv, lba, count, buf);
}
static int bd_write(blockdev_t* bd, uint32_t lba, uint32_t count, const void* buf) {
    int i = (int)(uintptr_t)bd->priv;
    return virtio_blk_write(i, lba, count, buf) == 0 ? virtio_blk_flush(i) : -1;
}

static int start(vblk_t* v, const pci_dev_t* pd) {
    v->pci=*pd;
    if (vio_open(&v->vio, pd) != 0) return -1;
    uint32_t feat = vio_features(&v->vio);
    if (vio_set_features(&v->vio, feat & (F_RO | F_FLUSH)) != 0) { vio_failed(&v->vio); return -1; }
    v->ro = (feat & F_RO) != 0;
    v->flush = (feat & F_FLUSH) != 0;
    uint64_t cap = (uint64_t)vio_cfg32(&v->vio, 0) | (uint64_t)vio_cfg32(&v->vio, 4) << 32;
    v->sectors = cap > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)cap;
    if(!v->ring)v->ring = (uint8_t*)page_alloc(VIO_QBYTES);
    if(!v->bounce)v->bounce = (uint8_t*)page_alloc(CHUNK);
    if(!v->req)v->req = (vreq_t*)page_alloc(64);
    if (!v->ring || !v->bounce || !v->req) { vio_failed(&v->vio); return -1; }
    volatile uint8_t* used;
    uint16_t size = vio_queue(&v->vio, 0, v->ring, 128, &v->desc, &v->avail, &used);
    if (size < 3) { vio_failed(&v->vio); return -1; }
    v->used = used;
    v->status = (volatile uint8_t*)v->req + 32;
    v->size = size;
    v->last_used=0;v->avail_idx=0;
    /* interrupts when its interrupt line is one we can use; else polled */
    v->irq = pd->irq_line > 0 && pd->irq_line < 16;
    v->avail[0] = v->irq ? 0 : 1;                     /* (1: VRING_AVAIL_F_NO_INTERRUPT) */
    if (v->irq) irq_install(pd->irq_line, vblk_irq);
    vio_driver_ok(&v->vio);
    uint32_t mb = v->sectors / 2048;
    ksnprintf(v->model, sizeof(v->model), "virtio disk, %u %s%s", mb >= 10240 ? mb / 1024 : mb,
              mb >= 10240 ? "GiB" : "MiB", v->ro ? " (read-only)" : "");
    klog("virtio-blk: %s, %u sectors, queue %u%s%s\n", v->vio.modern ? "modern" : "legacy", v->sectors, size,
         v->flush ? ", flush" : "", v->ro ? ", read-only" : "");
    return 0;
}

static int scan_cb(const pci_dev_t* d, void* ctx) {
    (void)ctx;
    if (d->vendor != 0x1AF4 || (d->device != 0x1001 && d->device != 0x1042) || g_count >= VBLK_MAX) return 0;
    vblk_t* v = &g_vb[g_count];
    memset(v, 0, sizeof(*v));
    v->w.pid = -1;
    if (start(v, d) == 0) {
        v->bd = blockdev_register_kind("virtio", v->model, v->sectors, 512, bd_read, bd_write, (void*)(uintptr_t)g_count);
        g_count++;
    }
    return 0;
}

void virtio_blk_init(void) {
    pci_scan(scan_cb, NULL);
}
int virtio_blk_s3_resume(void) {
    for(int i=0;i<g_count;i++) {
        pci_dev_t d=g_vb[i].pci;
        if(start(&g_vb[i],&d))return -1;
    }
    return 0;
}
