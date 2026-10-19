/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KBLAYOUTS_H
#define KBLAYOUTS_H

#include "types.h"

/*
 * Keyboard layouts (kernel/kblayouts.c), each the whole keyboard by key
 * position: for every key that types something (set-1 scancode), what it
 * gives alone, with Shift, with AltGr (right Alt, or Ctrl+Alt) and with
 * Shift+AltGr. Code points (é is 0xE9), sent on as UTF-8; DEAD(c): a dead
 * accent - nothing yet, it goes on the next letter (^ then e: ê).
 */

#define DEAD(c)    (0x10000u | (c))
#define IS_DEAD(v) ((v) & 0x10000u)

typedef struct { uint8_t sc; uint32_t n, s, ag, sag; } kb_key_t;

typedef struct {
    const char*     name;        /* as saved and typed: "FR", "de_CH" */
    const char*     label;       /* "French (AZERTY)" */
    const kb_key_t* keys;        /* NULL: the US keyboard as it is */
    int             nkeys;
} kb_layout_desc_t;

extern const kb_layout_desc_t kb_layouts[];
extern const int              kb_layout_count;

/* accent + letter -> the accented letter (0: none) */
uint32_t kb_compose(uint32_t accent, uint32_t base);

#endif
