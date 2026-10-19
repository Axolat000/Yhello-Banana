#ifndef BOOTMEM_H
#define BOOTMEM_H
#include "types.h"

/* Usable firmware RAM in KiB, preferring the complete Multiboot2 map.
 * Basic memory information is only a fallback: it stops at the PCI hole. */
uint32_t bootmem_kb_mb2(const void* data, uint32_t size);
/* One available page below 0x90000, above firmware's low-memory scratch
 * area. The kernel heap never uses these pages. Zero if none is available. */
uint32_t bootmem_wake_page_mb2(const void* data,uint32_t size);
#endif
