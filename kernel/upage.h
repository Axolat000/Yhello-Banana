#ifndef UPAGE_H
#define UPAGE_H

#include "types.h"

/*
 * 4 KiB pages for apps' mmap regions (kernel/uspace.c), shared by every
 * app: taken from 2 MiB chunks (RAM above 4 GiB first, else the kernel
 * heap) and a chunk goes back as soon as none of its pages is in use - so
 * the memory an app unmaps, or that is compressed away (kernel/zswap.c),
 * is the system's again, not kept by that app until it exits.
 */

void*    upage_alloc(void);          /* a page (not zeroed), or NULL */
void     upage_free(void* p);
uint64_t upage_used_bytes(void);     /* pages handed out */
uint64_t upage_held_bytes(void);     /* chunks taken for them */

#endif
