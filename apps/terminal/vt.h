/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef VT_H
#define VT_H
/*
 * The terminal's screen: what programs write (xterm-256color - VT220
 * escapes, colours incl. 24-bit, scrolling regions, the alternate screen,
 * mouse reporting, bracketed paste, OSC titles, UTF-8 with double-width
 * characters) turned into rows of cells, with a scrollback. No drawing
 * here: main.c draws the cells (and tools/tests/terminal_vt checks this
 * file on Linux).
 */
#include <stddef.h>
#include <stdint.h>

/* a colour: VT_DEFAULT, VT_PAL | 0..255, or VT_RGB | 0xRRGGBB */
#define VT_DEFAULT 0u
#define VT_PAL     0x01000000u
#define VT_RGB     0x02000000u

#define VT_BOLD      0x001
#define VT_FAINT     0x002
#define VT_ITALIC    0x004
#define VT_UNDERLINE 0x008
#define VT_BLINK     0x010
#define VT_REVERSE   0x020
#define VT_INVISIBLE 0x040
#define VT_STRIKE    0x080
#define VT_WIDE      0x100      /* the first half of a double-width character */
#define VT_WIDE_CONT 0x200      /* its second half (ch 0) */

typedef struct {
    uint32_t ch;                /* a code point; 0: empty */
    uint32_t fg, bg;
    uint16_t attr;
} vt_cell_t;

typedef struct {
    vt_cell_t* c;
    int        len;             /* cells (the width when it was made) */
    int        wrapped;         /* it goes on in the next line (autowrap) */
} vt_line_t;

/* mouse reporting (DECSET 1000 / 1002 / 1003) */
enum { VT_MOUSE_OFF = 0, VT_MOUSE_CLICK, VT_MOUSE_DRAG, VT_MOUSE_ANY };

typedef struct {
    int x, y, pending;          /* pending: at the right margin, the next character wraps */
    uint32_t fg, bg;
    uint16_t attr;
    int origin, g0_lines, g1_lines, gl1;
} vt_cursor_t;

typedef struct vt {
    int cols, rows;
    vt_line_t* pri;             /* the screens: rows lines each */
    vt_line_t* alt;
    vt_line_t* scr;             /* the one shown */
    int alt_on;

    vt_cursor_t cur, saved_pri, saved_alt;
    int top, bot;               /* scrolling region (rows, inclusive) */
    uint8_t* tabs;              /* a tab stop at each column set */

    /* modes */
    int autowrap, insert, newline_mode, cursor_visible, app_cursor, app_keypad;
    int mouse, mouse_sgr, paste_bracket, focus_events, reverse_video;

    /* scrollback (the main screen's lines that went off the top) */
    vt_line_t* sb;
    int sb_max, sb_count, sb_head;          /* sb_head: the oldest */

    /* parser */
    int state;
    int params[24], nparams, sub[24];
    char priv, inter;
    char osc[512];
    int osc_len;
    uint32_t utf8_cp;
    int utf8_left;
    uint32_t last_ch;           /* (REP) */

    /* for main.c */
    uint8_t* dirty;             /* per row */
    int all_dirty;
    char title[256];
    int title_changed, bell;
    char reply[512];            /* answers for the program (DSR, DA): to write to it */
    int reply_len;
    long scrolled;              /* lines scrolled into the scrollback (for selections) */
} vt_t;

vt_t* vt_new(int cols, int rows, int scrollback);
void  vt_free(vt_t* vt);
void  vt_write(vt_t* vt, const char* data, size_t n);
void  vt_resize(vt_t* vt, int cols, int rows);
void  vt_reset(vt_t* vt);
/* line i of what can be shown: -sb_count .. -1 the scrollback (-1 the
 * newest), 0 .. rows-1 the screen; NULL out of range */
const vt_line_t* vt_get_line(const vt_t* vt, int i);
/* the bytes a key sends, given the modes (app cursor keys...): its length */
int   vt_key_seq(const vt_t* vt, int key, int mods, char* out);
/* a mouse event for the program, if it asked for them: its length (0: not
 * wanted). button: 0 left, 1 middle, 2 right, 64 / 65 the wheel up / down,
 * -1 none (a move with no button held); kind: VT_MOUSE_PRESS / RELEASE / MOTION */
enum { VT_MOUSE_PRESS, VT_MOUSE_RELEASE, VT_MOUSE_MOTION };
int   vt_mouse_seq(const vt_t* vt, int button, int kind, int col, int row, int mods, char* out);
/* the terminal's colour for c (VT_DEFAULT: def) as 0xRRGGBB */
uint32_t vt_rgb(uint32_t c, uint32_t def);

/* keys for vt_key_seq (characters are sent as they are) */
enum {
    VT_KEY_UP = 0x101, VT_KEY_DOWN, VT_KEY_LEFT, VT_KEY_RIGHT, VT_KEY_HOME, VT_KEY_END,
    VT_KEY_PGUP, VT_KEY_PGDN, VT_KEY_DELETE, VT_KEY_INSERT,
    VT_KEY_F1 = 0x111            /* .. F12: + 0..11 */
};
#define VT_MOD_SHIFT 1
#define VT_MOD_CTRL  2
#define VT_MOD_ALT   4

#endif
