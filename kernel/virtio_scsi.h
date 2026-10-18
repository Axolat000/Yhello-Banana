#ifndef VIRTIO_SCSI_H
#define VIRTIO_SCSI_H

#include "types.h"
#include "ata.h"

/* virtio-scsi controllers (QEMU / KVM, Proxmox's "VirtIO SCSI"): the SCSI
 * disks behind them, for install/sync and in /mnt/scsi */

#define VSCSI_CTL_MAX 4
#define VSCSI_MAX     8

struct blockdev;

void virtio_scsi_init(void);                 /* finds the controllers and their disks */
int  virtio_scsi_probe(ata_disk_t* out, int max);
int  virtio_scsi_read(int idx, uint32_t lba, uint32_t count, void* buf);
int  virtio_scsi_write(int idx, uint32_t lba, uint32_t count, const void* buf);
int  virtio_scsi_flush(int idx);
struct blockdev* virtio_scsi_blockdev(int idx);

#endif
