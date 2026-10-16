#include "disk.h"
#include "atapi.h"
#include "ahci.h"
#include "nvme.h"
#include "virtio_blk.h"
#include "kstring.h"
#include "blockdev.h"

int disk_probe(ata_disk_t* out, int max) {
    ata_disk_t ide[4];
    ata_probe_disks(ide);
    int n = 0;
    for (int i = 0; i < 4 && n < max; i++)
        if (ide[i].present) out[n++] = ide[i];
    n += ahci_probe(out + n, max - n);
    n += nvme_probe(out + n, max - n);
    n += virtio_blk_probe(out + n, max - n);
    return n;
}

int disk_read(const ata_disk_t* d, uint32_t lba, uint32_t count, void* buf) {
    if (d->nvme) return nvme_read(d->nvme - 1, lba, count, buf);
    if (d->vblk) return virtio_blk_read(d->vblk - 1, lba, count, buf);
    if (d->ahci_port >= 0) return ahci_read(d->ahci_port, lba, count, buf);
    uint8_t* b = (uint8_t*)buf;
    while (count) {
        uint8_t n = (uint8_t)(count > 255 ? 255 : count);
        if (ata_read_sectors(d->bus, d->is_slave, lba, n, b) != 0) return -1;
        lba += n;
        count -= n;
        b += (uint32_t)n * 512;
    }
    return 0;
}

int disk_write(const ata_disk_t* d, uint32_t lba, uint32_t count, const void* buf) {
    if (d->nvme) return nvme_write(d->nvme - 1, lba, count, buf);
    if (d->vblk) return virtio_blk_write(d->vblk - 1, lba, count, buf);
    if (d->ahci_port >= 0) return ahci_write(d->ahci_port, lba, count, buf);
    const uint8_t* b = (const uint8_t*)buf;
    while (count) {
        uint8_t n = (uint8_t)(count > 255 ? 255 : count);
        if (ata_write_sectors(d->bus, d->is_slave, lba, n, b) != 0) return -1;
        lba += n;
        count -= n;
        b += (uint32_t)n * 512;
    }
    return 0;
}

int disk_flush(const ata_disk_t* d) {
    if (d->nvme) return nvme_flush(d->nvme - 1);
    if (d->vblk) return virtio_blk_flush(d->vblk - 1);
    return d->ahci_port >= 0 ? ahci_flush(d->ahci_port) : 0;   /* ata.c flushes after each write */
}

int disk_cd_read(const ata_disk_t* d, uint32_t lba, uint32_t count, void* buf) {
    if (d->ahci_port >= 0) return ahci_atapi_read(d->ahci_port, lba, count, buf);
    return atapi_read_blocks(d->bus, d->is_slave, lba, count, buf);
}

int disk_cd_iso_size(const ata_disk_t* d, uint32_t* out_bytes) {
    if (d->ahci_port < 0) return atapi_iso_size(d->bus, d->is_slave, out_bytes);
    static uint8_t pvd[2048] __attribute__((aligned(16)));
    if (ahci_atapi_read(d->ahci_port, 16, 1, pvd) != 0) return -1;
    if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) return -1;
    uint32_t blocks = (uint32_t)pvd[80] | (uint32_t)pvd[81] << 8 | (uint32_t)pvd[82] << 16 | (uint32_t)pvd[83] << 24;
    *out_bytes = blocks * 2048u;
    return 0;
}

void disk_describe(const ata_disk_t* d, char* out, int cap) {
    if (d->nvme) ksnprintf(out, (size_t)cap, "NVMe #%d", d->nvme - 1);
    else if (d->vblk) ksnprintf(out, (size_t)cap, "virtio #%d", d->vblk - 1);
    else if (d->ahci_port >= 0) ksnprintf(out, (size_t)cap, "SATA #%d", d->ahci_port);
    else ksnprintf(out, (size_t)cap, "IDE %s %s", d->bus == ATA_BUS_PRIMARY ? "primary" : "secondary",
                   d->is_slave ? "slave" : "master");
}

/* ── the extra IDE / SATA disks as block devices ─────────────────────
 * NVMe and virtio disks register themselves (kernel/nvme.c, virtio_blk.c);
 * the IDE and SATA hard disks did not, so a second disk's partitions were
 * never mounted. Every one but a disk holding Banana OS becomes a block
 * device here, its volumes mounted by the automount task (/mnt/sata, /mnt/ide). */

static ata_disk_t g_extra[DISK_MAX];
static int        g_nextra;

static int extra_read(blockdev_t* bd, uint32_t lba, uint32_t count, void* buf) {
    return disk_read((const ata_disk_t*)bd->priv, lba, count, buf);
}

static int extra_write(blockdev_t* bd, uint32_t lba, uint32_t count, const void* buf) {
    const ata_disk_t* d = (const ata_disk_t*)bd->priv;
    if (disk_write(d, lba, count, buf) != 0) return -1;
    return disk_flush(d);
}

int disk_same(const ata_disk_t* a, const ata_disk_t* b) {
    if (a->nvme || b->nvme) return a->nvme == b->nvme;
    if (a->vblk || b->vblk) return a->vblk == b->vblk;
    if (a->ahci_port >= 0 || b->ahci_port >= 0) return a->ahci_port == b->ahci_port;
    return a->bus == b->bus && a->is_slave == b->is_slave;
}

blockdev_t* disk_blockdev(const ata_disk_t* d) {
    char name[16];
    if (d->nvme) ksnprintf(name, sizeof(name), "nvme%d", d->nvme - 1);
    else if (d->vblk) ksnprintf(name, sizeof(name), "virtio%d", d->vblk - 1);
    else {
        for (int i = 0; i < BLOCKDEV_MAX; i++) {
            blockdev_t* bd = blockdev_get(i);
            if (bd && (bd->read == extra_read) && disk_same((const ata_disk_t*)bd->priv, d)) return bd;
        }
        return NULL;
    }
    return blockdev_find(name);
}

void disk_attach_extra(int (*holds_system)(const ata_disk_t* d)) {
    ata_disk_t all[DISK_MAX];
    int n = disk_probe(all, DISK_MAX);
    for (int i = 0; i < n && g_nextra < DISK_MAX; i++) {
        const ata_disk_t* d = &all[i];
        if (d->is_atapi || !d->sectors) continue;
        if (holds_system && holds_system(d)) {             /* Banana OS's own disk: never mounted */
            blockdev_set_system(disk_blockdev(d));
            continue;
        }
        if (d->nvme || d->vblk || disk_blockdev(d)) continue;   /* (already block devices) */
        g_extra[g_nextra] = *d;
        const char* kind = d->ahci_port >= 0 ? "sata" : "ide";
        blockdev_t* bd = blockdev_register_kind(kind, d->model[0] ? d->model : "hard disk", d->sectors, 512,
                                                extra_read, extra_write, &g_extra[g_nextra]);
        if (bd) g_nextra++;
    }
    blockdev_start_mounting();
}
