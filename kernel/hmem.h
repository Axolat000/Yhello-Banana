/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HMEM_H
#define HMEM_H

#include "types.h"

/*
 * RAM above 4 GiB (64-bit kernel). The kernel heap (kernel/kheap.c) stays
 * below 3.5 GiB - its buffers double as DMA buffers for devices that only
 * take 32-bit addresses. What the firmware's memory map lists above 4 GiB
 * is identity-mapped at boot and handed out here in 2 MiB frames: apps'
 * memory comes from it first (kernel/umem.c), so a PC with 16 GiB can use
 * all of it. Up to 512 GiB (the reach of an app's page tables).
 *
 * The 32-bit kernel cannot address it: hmem_init finds nothing there.
 */

void     hmem_init(uint32_t mb2_info_addr);
/* n frames (2 MiB each) in a row: the physical (= virtual) address, 0 if none */
uint64_t hmem_alloc(uint32_t n);
void     hmem_free(uint64_t addr, uint32_t n);

uint64_t hmem_total_bytes(void);
uint64_t hmem_used_bytes(void);

#endif
