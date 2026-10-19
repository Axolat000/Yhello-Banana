/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdlib.h>
#define kmalloc(n) malloc(n)
#define kzalloc(n) calloc(1, n)
#define krealloc(p, n) realloc(p, n)
#define kfree(p) free(p)
static inline uint32_t kheap_total_bytes(void) { return 512u << 20; }
static inline uint32_t kheap_used_bytes(void) { return 64u << 20; }
