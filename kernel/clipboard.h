/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include "types.h"

/* The system-wide clipboard: one text, shared by terminals, the editor
 * and the web browser. */

#define CLIPBOARD_MAX (64u * 1024u)

void        clipboard_set(const char* text, uint32_t len);
const char* clipboard_get(uint32_t* len);     /* "" when empty, never NULL */
uint32_t    clipboard_generation(void);       /* bumped on every change */

/* The clipboard history (Win+V): the last CLIP_HIST_MAX texts copied, the
 * newest first - copying one that is there already moves it to the top.
 * Pinned ones are never pushed out or cleared, and are kept across
 * restarts (/etc/clipboard-pinned). Safe from any core (apps copy through
 * system calls); the pinned file is read and written by the desktop only. */
#define CLIP_HIST_MAX 25

int      clipboard_hist_count(void);
/* entry i (0: the newest): up to cap-1 bytes of it into out (NUL-ended),
 * its whole length in *len, pinned or not; 0, or -1 if there is no entry i */
int      clipboard_hist_peek(int i, char* out, uint32_t cap, uint32_t* len, int* pinned);
/* entry i becomes the clipboard (and the newest entry): 0 / -1 */
int      clipboard_hist_use(int i);
int      clipboard_hist_delete(int i);
int      clipboard_hist_pin(int i, int on);
/* removes every entry not pinned: how many */
int      clipboard_hist_clear(void);
uint32_t clipboard_hist_generation(void);     /* bumped on every change */
/* the pinned entries saved last time (once, when the desktop starts) */
void     clipboard_hist_load(void);

#endif
