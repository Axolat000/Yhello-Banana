#ifndef KHEAP_H
#define KHEAP_H

#include "types.h"

/* Kernel heap: a first-fit, address-ordered free list with splitting and
 * coalescing, laid over the RAM between the end of the kernel image and
 * the end of the usable memory region it sits in (from the Multiboot2
 * memory map). No paging here, so heap pointers are physical addresses,
 * which is also what makes them directly usable as DMA buffers. */

void  kheap_init(uint32_t mb2_info_addr);
void* kmalloc(size_t size);                 /* 16-byte aligned, NULL on OOM */
void* kzalloc(size_t size);                 /* zeroed */
/* starting at a multiple of align (a power of two: page tables, the 2 MiB
 * blocks of apps' memory); freed with kfree */
void* kmalloc_aligned(size_t size, size_t align);
void* krealloc(void* ptr, size_t size);     /* grows in place when it can */
void  kfree(void* ptr);

uint32_t kheap_total_bytes(void);
uint32_t kheap_used_bytes(void);
uint32_t kheap_largest_free(void);

/* called when an allocation finds no room, to free some (need: bytes in
 * one block) before it tries again - fs.c drops cached file data */
void kheap_set_reclaim(uint32_t (*reclaim)(uint32_t need));
/* Explicit, potentially expensive diagnostic: 0 if healthy, 1 on the first
 * detected error (copied to err when non-NULL and cap > 0). The physical list
 * must be readable; corrupt bin pointers are checked before dereferencing. */
int      kheap_check(char* err, int cap);

/* [lo, hi) becomes heap too (memory the kernel image no longer needs) */
void kheap_add_region(uintptr_t lo, uintptr_t hi);

#endif
