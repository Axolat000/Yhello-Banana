/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crc32c.h"

/* table-driven, 8 bits at a time; the table is made on first use */
static uint32_t g_tab[256];
static int      g_made;

static void make_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0x82F63B78u : c >> 1;
        g_tab[i] = c;
    }
    g_made = 1;
}

uint32_t crc32c(uint32_t crc, const void* data, uint32_t len) {
    if (!g_made) make_table();
    const uint8_t* p = (const uint8_t*)data;
    crc = ~crc;
    while (len--) crc = g_tab[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}
