/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SDHCI_H
#define SDHCI_H

#include "types.h"
#include "ata.h"

/* SD card readers and eMMC chips (SD Host Controller, PCI class 08:05):
 * cards are mounted under /mnt/sd as they are put in, unmounted when taken
 * out; eMMC under /mnt/mmc. Both are disks to install on. */

#define SDHCI_MAX 4

struct blockdev;

void sdhci_init(void);
/* the slots with a working card or chip, as disks (kernel/disk.c) */
int  sdhci_probe(ata_disk_t* out, int max);
int  sdhci_read(int slot, uint32_t lba, uint32_t count, void* buf);
int  sdhci_write(int slot, uint32_t lba, uint32_t count, const void* buf);
int  sdhci_is_emmc(int slot);
struct blockdev* sdhci_blockdev(int slot);

#endif
