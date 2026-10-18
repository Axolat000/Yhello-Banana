#ifndef ATA_H
#define ATA_H

#include "types.h"

/* Minimal 28-bit LBA ATA PIO driver (primary + secondary bus, master +
 * slave). Only handles plain ATA hard disks - ATAPI (CD/DVD) devices are
 * detected and skipped, so this never touches the GRUB boot CD. */

#define ATA_BUS_PRIMARY   0
#define ATA_BUS_SECONDARY 1

typedef struct {
    int      present;   /* drive responded to IDENTIFY/IDENTIFY PACKET */
    int      is_atapi;  /* CD/DVD (ATAPI) - never a valid install target */
    int      bus;        /* ATA_BUS_PRIMARY / ATA_BUS_SECONDARY */
    int      is_slave;
    uint32_t sectors;    /* sector count (0 if unknown/ATAPI); up to 2 TB */
    char     model[41];  /* IDENTIFY model string, NUL-terminated */
    int      ahci_port;  /* SATA (kernel/ahci.c) device index, or -1 for an IDE drive */
    int      nvme;       /* NVMe drive (kernel/nvme.c) index + 1, or 0 */
    int      vblk;       /* virtio-blk disk (kernel/virtio_blk.c) index + 1, or 0 */
    int      scsi;       /* SCSI disk (kernel/virtio_scsi.c) index + 1, or 0 */
    int      mmc;        /* eMMC chip or SD card (kernel/sdhci.c) slot index + 1, or 0 */
} ata_disk_t;

/* Probes all 4 possible drives (primary/secondary x master/slave) into
 * out[0..3] (out must have room for 4). Returns the number of entries with
 * present == 1 (both ATA and ATAPI). */
int ata_probe_disks(ata_disk_t* out);

/* Reads/writes `count` consecutive 512-byte sectors starting at `lba`
 * to/from buf (buf must be count*512 bytes). Returns 0 on success, -1 on
 * error or timeout. count must be 1-255 (0 is reserved by the ATA spec
 * for "256 sectors", not supported here). */
int ata_read_sectors(int bus, int is_slave, uint32_t lba, uint8_t count, void* buf);
int ata_write_sectors(int bus, int is_slave, uint32_t lba, uint8_t count, const void* buf);
/* the drive's write cache onto the disk (the writes above only reach the cache) */
int ata_flush(int bus, int is_slave);

/* bus-master DMA (the PCI IDE controller): 1 if there is one */
int  ata_dma_available(void);
/* count sectors (up to 256, or 65535 with 48-bit addressing) by DMA into /
 * out of buf (below 4 GiB, even): 0, -1 (the drive failed), -2 (no DMA here:
 * use PIO) */
int  ata_dma_rw(int bus, int is_slave, uint32_t lba, uint32_t count, void* buf, int write);
void ata_dma_disable(void);
/* sectors one DMA command takes on that drive */
uint32_t ata_dma_max(int bus, int is_slave);
void ata_lock(int bus);
void ata_unlock(int bus);

#endif
