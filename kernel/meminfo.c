/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "meminfo.h"
#include "kheap.h"
#include "fs.h"
#include "sysinfo.h"
#include "hmem.h"
#include "builtin_apps.h"

extern char _kernel_end[];   /* boot/linker.ld: the image starts at 1 MB */

void meminfo_get(meminfo_t* m) {
    uint32_t kernel = (uint32_t)(uintptr_t)_kernel_end - 0x100000u - builtin_released_bytes();   /* (given to the heap) */
    uint32_t heap_total = kheap_total_bytes(), heap_used = kheap_used_bytes();
    uint32_t high_kb = (uint32_t)(hmem_total_bytes() >> 10), high_used_kb = (uint32_t)(hmem_used_bytes() >> 10);
    m->kernel_kb = (kernel + 1023u) >> 10;
    m->total_kb = m->kernel_kb + (heap_total >> 10) + high_kb;
    m->used_kb = m->kernel_kb + ((heap_used + 1023u) >> 10) + high_used_kb;
    m->free_kb = ((heap_total - heap_used) >> 10) + (high_kb - high_used_kb);
    m->files_kb = (fs_ram_used_bytes() + 1023u) >> 10;
    m->cached_kb = fs_evictable_bytes() >> 10;
    m->avail_kb = m->free_kb + m->cached_kb;
    m->largest_kb = kheap_largest_free() >> 10;
    m->installed_kb = sysinfo_get()->mem_kb;
    if (m->installed_kb < m->total_kb) m->installed_kb = m->total_kb;
}
