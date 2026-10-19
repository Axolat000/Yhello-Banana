/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Compile the actual kernel decoder against host heap/string shims. */
#include "types.h"
#include "kheap.h"
#include "kstring.h"
static uint32_t kheap_largest_free(void) { return 0xffffffffu; }
static void memset32(void* dst,uint32_t value,uint32_t count) {
    uint32_t* p=dst;while(count--)*p++=value;
}
#include "../../kernel/image.c"
