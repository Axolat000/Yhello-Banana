#ifndef SDHCI_H
#define SDHCI_H

#include "types.h"

/* SD card readers (SD Host Controller, PCI class 08:05): cards are
 * mounted under /mnt/sd as they are put in, unmounted when taken out */

#define SDHCI_MAX 4

void sdhci_init(void);

#endif
