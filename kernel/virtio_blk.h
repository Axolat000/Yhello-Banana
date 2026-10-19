/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "types.h"
#include "ata.h"

/* virtio-blk disks (QEMU / KVM / Proxmox / cloud VMs), legacy interface */

#define VBLK_MAX 4

void virtio_blk_init(void);                  /* finds and starts the disks */
int virtio_blk_s3_resume(void);              /* reuse the existing device/ring/buffers */
int  virtio_blk_probe(ata_disk_t* out, int max);
int  virtio_blk_read(int idx, uint32_t lba, uint32_t count, void* buf);
int  virtio_blk_write(int idx, uint32_t lba, uint32_t count, const void* buf);
int  virtio_blk_flush(int idx);

#endif
