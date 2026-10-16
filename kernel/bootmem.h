#ifndef BOOTMEM_H
#define BOOTMEM_H
#include "types.h"

/* Usable firmware RAM in KiB, preferring the complete Multiboot2 map.
 * Basic memory information is only a fallback: it stops at the PCI hole. */
uint32_t bootmem_kb_mb2(const void* data, uint32_t size);
#endif
