/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The terminal's screen and its escape sequences (see vt.h). What xterm
 * does is the reference; ctlseqs.html's names are used for the sequences. */
#ifndef VT_KERNEL             /* (the kernel's consoles build it too: kernel/kvt.c) */
#define _XOPEN_SOURCE 700     /* (wcwidth, when tested on Linux) */
#include "vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#endif

enum { S_GROUND, S_ESC, S_ESC_INTER, S_CSI, S_OSC, S_OSC_ESC, S_STRING, S_STRING_ESC };

/* ── lines ─────────────────────────────────────────────────────────── */

static void blank(vt_t* vt, vt_cell_t* c, int n) {
    /* (erased cells take the current background: xterm's bce) */
    for (int i = 0; i < n; i++) {
        c[i].ch = 0;
        c[i].fg = VT_DEFAULT;
        c[i].bg = vt->cur.bg;
        c[i].attr = 0;
    }
}

static int line_init(vt_t* vt, vt_line_t* l, int cols) {
    l->c = (vt_cell_t*)malloc(sizeof(vt_cell_t) * (size_t)(cols > 0 ? cols : 1));
    if (!l->c) { l->len = 0; return -1; }
    l->len = cols;
    l->wrapped = 0;
    uint32_t keep = vt->cur.bg;
    vt->cur.bg = VT_DEFAULT;
    blank(vt, l->c, cols);
    vt->cur.bg = keep;
    return 0;
}

static vt_line_t* screen_new(vt_t* vt, int cols, int rows) {
    vt_line_t* s = (vt_line_t*)calloc((size_t)rows, sizeof(vt_line_t));
    if (!s) return NULL;
    for (int i = 0; i < rows; i++) line_init(vt, &s[i], cols);
    return s;
}

static void screen_free(vt_line_t* s, int rows) {
    if (!s) return;
    for (int i = 0; i < rows; i++) free(s[i].c);
    free(s);
}

static void dirty(vt_t* vt, int y) {
    if (y >= 0 && y < vt->rows) vt->dirty[y] = 1;
}

static void dirty_range(vt_t* vt, int a, int b) {
    for (int y = a; y <= b; y++) dirty(vt, y);
}

/* the scrollback takes a line of the main screen (its cells move there) */
static void sb_push(vt_t* vt, vt_line_t* l) {
    if (vt->sb_max <= 0) { free(l->c); return; }
    /* (only the cells in use: trailing blanks with no colour are dropped) */
    int n = l->len;
    while (n > 0 && !l->c[n - 1].ch && l->c[n - 1].bg == VT_DEFAULT && !l->c[n - 1].attr) n--;
    vt_line_t keep = { l->c, n, l->wrapped };
    if (n < l->len) {
        vt_cell_t* c = (vt_cell_t*)realloc(l->c, sizeof(vt_cell_t) * (size_t)(n ? n : 1));
        if (c) keep.c = c;
    }
    if (vt->sb_count == vt->sb_max) {
        vt_line_t* old = &vt->sb[vt->sb_head];
        free(old->c);
        *old = keep;
        vt->sb_head = (vt->sb_head + 1) % vt->sb_max;
    } else {
        vt->sb[(vt->sb_head + vt->sb_count) % vt->sb_max] = keep;
        vt->sb_count++;
    }
    vt->scrolled++;
}

/* rows top..bot move up n lines (new blank ones at the bottom) */
static void scroll_up(vt_t* vt, int top, int bot, int n) {
    if (n <= 0) return;
    int h = bot - top + 1;
    if (n > h) n = h;
    for (int i = 0; i < n; i++) {
        vt_line_t gone = vt->scr[top];
        memmove(&vt->scr[top], &vt->scr[top + 1], sizeof(vt_line_t) * (size_t)(h - 1));
        if (!vt->alt_on && top == 0) {
            sb_push(vt, &gone);
            line_init(vt, &vt->scr[bot], vt->cols);
        } else {
            vt->scr[bot] = gone;
        }
        blank(vt, vt->scr[bot].c, vt->scr[bot].len);
        vt->scr[bot].wrapped = 0;
    }
    dirty_range(vt, top, bot);
}

static void scroll_down(vt_t* vt, int top, int bot, int n) {
    if (n <= 0) return;
    int h = bot - top + 1;
    if (n > h) n = h;
    for (int i = 0; i < n; i++) {
        vt_line_t gone = vt->scr[bot];
        memmove(&vt->scr[top + 1], &vt->scr[top], sizeof(vt_line_t) * (size_t)(h - 1));
        vt->scr[top] = gone;
        blank(vt, gone.c, gone.len);
        vt->scr[top].wrapped = 0;
    }
    dirty_range(vt, top, bot);
}

/* ── setup ─────────────────────────────────────────────────────────── */

static void reset_tabs(vt_t* vt) {
    for (int i = 0; i < vt->cols; i++) vt->tabs[i] = i && i % 8 == 0;
}

static void reset_modes(vt_t* vt) {
    memset(&vt->cur, 0, sizeof(vt->cur));
    vt->saved_pri = vt->saved_alt = vt->cur;
    vt->top = 0;
    vt->bot = vt->rows - 1;
    vt->autowrap = 1;
    vt->insert = vt->newline_mode = 0;
    vt->cursor_visible = 1;
    vt->app_cursor = vt->app_keypad = 0;
    vt->mouse = VT_MOUSE_OFF;
    vt->mouse_sgr = vt->paste_bracket = vt->focus_events = vt->reverse_video = 0;
    vt->state = S_GROUND;
    vt->utf8_left = 0;
}

vt_t* vt_new(int cols, int rows, int scrollback) {
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    vt_t* vt = (vt_t*)calloc(1, sizeof(vt_t));
    if (!vt) return NULL;
    vt->cols = cols;
    vt->rows = rows;
    vt->pri = screen_new(vt, cols, rows);
    vt->alt = screen_new(vt, cols, rows);
    vt->tabs = (uint8_t*)malloc((size_t)cols);
    vt->dirty = (uint8_t*)calloc((size_t)rows, 1);
    vt->sb_max = scrollback;
    vt->sb = scrollback > 0 ? (vt_line_t*)calloc((size_t)scrollback, sizeof(vt_line_t)) : NULL;
    if (!vt->pri || !vt->alt || !vt->tabs || !vt->dirty || (scrollback > 0 && !vt->sb)) { vt_free(vt); return NULL; }
    vt->scr = vt->pri;
    reset_modes(vt);
    reset_tabs(vt);
    vt->all_dirty = 1;
    return vt;
}

void vt_free(vt_t* vt) {
    if (!vt) return;
    screen_free(vt->pri, vt->rows);
    screen_free(vt->alt, vt->rows);
    for (int i = 0; i < vt->sb_count; i++) free(vt->sb[(vt->sb_head + i) % vt->sb_max].c);
    free(vt->sb);
    free(vt->tabs);
    free(vt->dirty);
    free(vt);
}

static void clear_screen(vt_t* vt, vt_line_t* s) {
    for (int y = 0; y < vt->rows; y++) {
        blank(vt, s[y].c, s[y].len);
        s[y].wrapped = 0;
    }
}

void vt_reset(vt_t* vt) {
    vt->alt_on = 0;
    vt->scr = vt->pri;
    reset_modes(vt);
    reset_tabs(vt);
    clear_screen(vt, vt->pri);
    clear_screen(vt, vt->alt);
    vt->all_dirty = 1;
}

/* a line of a screen to cols cells */
static void line_fit(vt_t* vt, vt_line_t* l, int cols) {
    if (l->len == cols) return;
    vt_cell_t* c = (vt_cell_t*)realloc(l->c, sizeof(vt_cell_t) * (size_t)cols);
    if (!c) return;
    if (cols > l->len) {
        uint32_t keep = vt->cur.bg;
        vt->cur.bg = VT_DEFAULT;
        blank(vt, c + l->len, cols - l->len);
        vt->cur.bg = keep;
    } else if (cols > 0 && (c[cols - 1].attr & VT_WIDE)) {
        c[cols - 1].ch = ' ';                   /* (half a wide character) */
        c[cols - 1].attr &= (uint16_t)~VT_WIDE;
    }
    l->c = c;
    l->len = cols;
}

static vt_line_t* screen_resize(vt_t* vt, vt_line_t* s, int cols, int rows, int* cy, int keep_in_sb) {
    /* fewer rows: the top ones go (into the scrollback, for the main
     * screen) as long as the cursor is below them; then the bottom ones */
    int drop_top = 0;
    if (rows < vt->rows) {
        int over = vt->rows - rows;
        int below = vt->rows - 1 - *cy;                 /* rows under the cursor */
        /* (blank rows at the bottom go first) */
        int blank_bottom = 0;
        for (int y = vt->rows - 1; y > *cy && blank_bottom < over; y--) {
            int empty = 1;
            for (int x = 0; x < s[y].len && empty; x++) if (s[y].c[x].ch) empty = 0;
            if (!empty) break;
            blank_bottom++;
        }
        (void)below;
        drop_top = over - blank_bottom;
        if (drop_top > *cy) drop_top = *cy;
    }
    vt_line_t* n = (vt_line_t*)calloc((size_t)rows, sizeof(vt_line_t));
    if (!n) return s;
    for (int y = 0; y < drop_top; y++) {
        line_fit(vt, &s[y], s[y].len);
        if (keep_in_sb) sb_push(vt, &s[y]);
        else free(s[y].c);
    }
    int y = 0;
    for (int from = drop_top; from < vt->rows; from++) {
        if (y < rows) {
            n[y] = s[from];
            line_fit(vt, &n[y], cols);
            y++;
        } else free(s[from].c);
    }
    uint32_t keep = vt->cur.bg;
    vt->cur.bg = VT_DEFAULT;
    for (; y < rows; y++) line_init(vt, &n[y], cols);
    vt->cur.bg = keep;
    free(s);
    *cy -= drop_top;
    return n;
}

void vt_resize(vt_t* vt, int cols, int rows) {
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols == vt->cols && rows == vt->rows) return;
    uint8_t* tabs = (uint8_t*)malloc((size_t)cols);
    uint8_t* d = (uint8_t*)calloc((size_t)rows, 1);
    if (!tabs || !d) { free(tabs); free(d); return; }
    int cy_pri = vt->alt_on ? vt->saved_pri.y : vt->cur.y;
    int cy_alt = vt->alt_on ? vt->cur.y : vt->saved_alt.y;
    vt->pri = screen_resize(vt, vt->pri, cols, rows, &cy_pri, 1);
    vt->alt = screen_resize(vt, vt->alt, cols, rows, &cy_alt, 0);
    vt->scr = vt->alt_on ? vt->alt : vt->pri;
    if (vt->alt_on) { vt->saved_pri.y = cy_pri; vt->cur.y = cy_alt; }
    else { vt->cur.y = cy_pri; vt->saved_alt.y = cy_alt; }
    free(vt->tabs);
    free(vt->dirty);
    vt->tabs = tabs;
    vt->dirty = d;
    vt->cols = cols;
    vt->rows = rows;
    reset_tabs(vt);
    vt->top = 0;
    vt->bot = rows - 1;
    vt_cursor_t* cs[3] = { &vt->cur, &vt->saved_pri, &vt->saved_alt };
    for (int i = 0; i < 3; i++) {
        if (cs[i]->x >= cols) cs[i]->x = cols - 1;
        if (cs[i]->y >= rows) cs[i]->y = rows - 1;
        if (cs[i]->y < 0) cs[i]->y = 0;
        cs[i]->pending = 0;
    }
    vt->all_dirty = 1;
}

const vt_line_t* vt_get_line(const vt_t* vt, int i) {
    if (i >= 0) return i < vt->rows ? &vt->scr[i] : NULL;
    if (vt->alt_on || -i > vt->sb_count) return NULL;
    return &vt->sb[(vt->sb_head + vt->sb_count + i) % vt->sb_max];
}

/* ── colours ───────────────────────────────────────────────────────── */

/* xterm's 16 colours (as its defaults, a little softer) */
static const uint32_t PAL16[16] = {
    0x000000, 0xCD3131, 0x0DBC79, 0xE5E510, 0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5,
    0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, 0x3B8EEA, 0xD670D6, 0x29B8DB, 0xFFFFFF,
};

uint32_t vt_rgb(uint32_t c, uint32_t def) {
    if (c & VT_RGB) return c & 0xFFFFFF;
    if (!(c & VT_PAL)) return def;
    unsigned i = c & 0xFF;
    if (i < 16) return PAL16[i];
    if (i < 232) {                                  /* the 6x6x6 cube */
        i -= 16;
        static const uint8_t lv[6] = { 0, 95, 135, 175, 215, 255 };
        return (uint32_t)lv[i / 36] << 16 | (uint32_t)lv[(i / 6) % 6] << 8 | lv[i % 6];
    }
    unsigned g = 8 + (i - 232) * 10;                /* the grey ramp */
    return g << 16 | g << 8 | g;
}

/* ── output ────────────────────────────────────────────────────────── */

static void reply(vt_t* vt, const char* s) {
    size_t n = strlen(s);
    if (vt->reply_len + (int)n <= (int)sizeof(vt->reply)) {
        memcpy(vt->reply + vt->reply_len, s, n);
        vt->reply_len += (int)n;
    }
}

static void cursor_to(vt_t* vt, int x, int y) {
    int top = vt->cur.origin ? vt->top : 0, bot = vt->cur.origin ? vt->bot : vt->rows - 1;
    if (x < 0) x = 0;
    if (x >= vt->cols) x = vt->cols - 1;
    if (y < top) y = top;
    if (y > bot) y = bot;
    dirty(vt, vt->cur.y);
    vt->cur.x = x;
    vt->cur.y = y;
    vt->cur.pending = 0;
    dirty(vt, y);
}

/* the cursor down a line, scrolling at the region's bottom (IND / LF) */
static void index_down(vt_t* vt) {
    vt->cur.pending = 0;
    if (vt->cur.y == vt->bot) scroll_up(vt, vt->top, vt->bot, 1);
    else if (vt->cur.y < vt->rows - 1) { dirty(vt, vt->cur.y); vt->cur.y++; dirty(vt, vt->cur.y); }
}

static void index_up(vt_t* vt) {
    vt->cur.pending = 0;
    if (vt->cur.y == vt->top) scroll_down(vt, vt->top, vt->bot, 1);
    else if (vt->cur.y > 0) { dirty(vt, vt->cur.y); vt->cur.y--; dirty(vt, vt->cur.y); }
}

/* DEC special graphics (ESC ( 0): the line-drawing characters */
static uint32_t dec_graphics(uint32_t c) {
    static const uint16_t map[32] = {
        0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1,     /* ` a b c d e f g */
        0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA,     /* h i j k l m n o */
        0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C,     /* p q r s t u v w */
        0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7, 0x0020,     /* x y z { | } ~ DEL */
    };
    return c >= 0x60 && c <= 0x7E ? map[c - 0x60] : c;
}

static int char_width(uint32_t c) {
    if (c < 0x300) return c >= 0x20 && (c < 0x7F || c >= 0xA0) ? 1 : 0;
    int w = wcwidth((wchar_t)c);
    return w < 0 ? 1 : w;
}

/* a character at the cursor */
static void put_char(vt_t* vt, uint32_t c) {
    if (vt->cur.gl1 ? vt->cur.g1_lines : vt->cur.g0_lines) c = dec_graphics(c);
    int w = char_width(c);
    if (w == 0) {
        /* a combining mark, a zero-width joiner...: (kept out: the cell
         * holds one code point) */
        return;
    }
    if (vt->cur.pending && vt->autowrap) {
        vt->scr[vt->cur.y].wrapped = 1;
        vt->cur.x = 0;
        index_down(vt);
    }
    if (w == 2 && vt->cur.x == vt->cols - 1) {
        /* no room for both halves: it goes to the next line */
        if (!vt->autowrap) return;
        vt_cell_t* l = vt->scr[vt->cur.y].c;
        blank(vt, &l[vt->cur.x], 1);
        vt->scr[vt->cur.y].wrapped = 1;
        vt->cur.x = 0;
        index_down(vt);
    }
    vt_line_t* line = &vt->scr[vt->cur.y];
    vt_cell_t* l = line->c;
    int x = vt->cur.x;
    if (vt->insert) {
        int n = vt->cols - x - w;
        if (n > 0) memmove(&l[x + w], &l[x], sizeof(vt_cell_t) * (size_t)n);
    }
    /* (writing over half of a wide character blanks its other half) */
    if (l[x].attr & VT_WIDE_CONT && x > 0) blank(vt, &l[x - 1], 1);
    if (x + w < vt->cols && (l[x + w - 1].attr & VT_WIDE)) blank(vt, &l[x + w], 1);
    l[x].ch = c;
    l[x].fg = vt->cur.fg;
    l[x].bg = vt->cur.bg;
    l[x].attr = (uint16_t)(vt->cur.attr | (w == 2 ? VT_WIDE : 0));
    if (w == 2) {
        l[x + 1].ch = 0;
        l[x + 1].fg = vt->cur.fg;
        l[x + 1].bg = vt->cur.bg;
        l[x + 1].attr = (uint16_t)(vt->cur.attr | VT_WIDE_CONT);
    }
    dirty(vt, vt->cur.y);
    vt->last_ch = c;
    if (x + w >= vt->cols) {
        vt->cur.x = vt->cols - 1;
        vt->cur.pending = 1;
    } else {
        vt->cur.x = x + w;
    }
}

static void control(vt_t* vt, uint8_t c) {
    switch (c) {
    case 7: vt->bell = 1; break;
    case 8:                                          /* BS */
        if (vt->cur.x > 0) { vt->cur.x -= vt->cur.pending ? 0 : 1; }
        vt->cur.pending = 0;
        dirty(vt, vt->cur.y);
        break;
    case 9: {                                        /* HT */
        int x = vt->cur.x + 1;
        while (x < vt->cols - 1 && !vt->tabs[x]) x++;
        vt->cur.x = x < vt->cols ? x : vt->cols - 1;
        vt->cur.pending = 0;
        break;
    }
    case 10: case 11: case 12:                       /* LF VT FF */
        index_down(vt);
        if (vt->newline_mode) vt->cur.x = 0;
        break;
    case 13: vt->cur.x = 0; vt->cur.pending = 0; dirty(vt, vt->cur.y); break;
    case 14: vt->cur.gl1 = 1; break;                 /* SO */
    case 15: vt->cur.gl1 = 0; break;                 /* SI */
    }
}

/* ── CSI ───────────────────────────────────────────────────────────── */

static int P(vt_t* vt, int i, int def) {
    return i < vt->nparams && vt->params[i] > 0 ? vt->params[i] : def;
}

static void erase_cells(vt_t* vt, int y, int x0, int x1) {
    if (y < 0 || y >= vt->rows) return;
    if (x0 < 0) x0 = 0;
    if (x1 > vt->cols) x1 = vt->cols;
    if (x0 >= x1) return;
    vt_cell_t* l = vt->scr[y].c;
    /* (a wide character cut in half goes whole) */
    if (x0 > 0 && (l[x0].attr & VT_WIDE_CONT)) x0--;
    if (x1 < vt->cols && (l[x1].attr & VT_WIDE_CONT)) x1++;
    blank(vt, &l[x0], x1 - x0);
    if (x1 >= vt->cols) vt->scr[y].wrapped = 0;
    dirty(vt, y);
}

static void sgr(vt_t* vt) {
    if (!vt->nparams) { vt->nparams = 1; vt->params[0] = 0; }
    for (int i = 0; i < vt->nparams; i++) {
        int p = vt->params[i];
        switch (p) {
        case 0: vt->cur.attr = 0; vt->cur.fg = vt->cur.bg = VT_DEFAULT; break;
        case 1: vt->cur.attr |= VT_BOLD; break;
        case 2: vt->cur.attr |= VT_FAINT; break;
        case 3: vt->cur.attr |= VT_ITALIC; break;
        case 4:
            if (i + 1 < vt->nparams && vt->sub[i + 1]) {     /* 4:0 no underline, 4:n a style */
                if (vt->params[i + 1] == 0) vt->cur.attr &= (uint16_t)~VT_UNDERLINE;
                else vt->cur.attr |= VT_UNDERLINE;
                i++;
            } else vt->cur.attr |= VT_UNDERLINE;
            break;
        case 5: case 6: vt->cur.attr |= VT_BLINK; break;
        case 7: vt->cur.attr |= VT_REVERSE; break;
        case 8: vt->cur.attr |= VT_INVISIBLE; break;
        case 9: vt->cur.attr |= VT_STRIKE; break;
        case 21: vt->cur.attr |= VT_UNDERLINE; break;
        case 22: vt->cur.attr &= (uint16_t)~(VT_BOLD | VT_FAINT); break;
        case 23: vt->cur.attr &= (uint16_t)~VT_ITALIC; break;
        case 24: vt->cur.attr &= (uint16_t)~VT_UNDERLINE; break;
        case 25: vt->cur.attr &= (uint16_t)~VT_BLINK; break;
        case 27: vt->cur.attr &= (uint16_t)~VT_REVERSE; break;
        case 28: vt->cur.attr &= (uint16_t)~VT_INVISIBLE; break;
        case 29: vt->cur.attr &= (uint16_t)~VT_STRIKE; break;
        case 39: vt->cur.fg = VT_DEFAULT; break;
        case 49: vt->cur.bg = VT_DEFAULT; break;
        case 38: case 48: case 58: {
            /* 38;5;n  38;2;r;g;b  and the colon forms 38:5:n  38:2:[id]:r:g:b */
            uint32_t col = VT_DEFAULT;
            int colon = i + 1 < vt->nparams && vt->sub[i + 1];
            int k = i + 1;
            if (k < vt->nparams && vt->params[k] == 5 && k + 1 < vt->nparams) {
                col = VT_PAL | (uint32_t)(vt->params[k + 1] & 255);
                k += 2;
            } else if (k < vt->nparams && vt->params[k] == 2) {
                int n = 0;
                for (int j = k + 1; j < vt->nparams && (!colon || vt->sub[j]); j++) n++;
                if (!colon) n = n > 3 ? 3 : n;
                int s = k + 1 + (colon && n >= 4 ? n - 3 : 0);     /* (skip the colour space id) */
                if (s + 2 < vt->nparams) {
                    col = VT_RGB | (uint32_t)(vt->params[s] & 255) << 16 | (uint32_t)(vt->params[s + 1] & 255) << 8
                        | (uint32_t)(vt->params[s + 2] & 255);
                }
                k = s + 3;
            } else {
                k = i + 1;
            }
            if (p == 38) vt->cur.fg = col;
            else if (p == 48) vt->cur.bg = col;
            i = k - 1;
            break;
        }
        default:
            if (p >= 30 && p <= 37) vt->cur.fg = VT_PAL | (uint32_t)(p - 30);
            else if (p >= 40 && p <= 47) vt->cur.bg = VT_PAL | (uint32_t)(p - 40);
            else if (p >= 90 && p <= 97) vt->cur.fg = VT_PAL | (uint32_t)(p - 90 + 8);
            else if (p >= 100 && p <= 107) vt->cur.bg = VT_PAL | (uint32_t)(p - 100 + 8);
        }
    }
}

static void save_cursor(vt_t* vt) {
    *(vt->alt_on ? &vt->saved_alt : &vt->saved_pri) = vt->cur;
}

static void restore_cursor(vt_t* vt) {
    dirty(vt, vt->cur.y);
    vt->cur = *(vt->alt_on ? &vt->saved_alt : &vt->saved_pri);
    if (vt->cur.x >= vt->cols) vt->cur.x = vt->cols - 1;
    if (vt->cur.y >= vt->rows) vt->cur.y = vt->rows - 1;
    dirty(vt, vt->cur.y);
}

static void set_alt(vt_t* vt, int on, int clear) {
    if (on == vt->alt_on) return;
    vt->alt_on = on;
    vt->scr = on ? vt->alt : vt->pri;
    if (on && clear) clear_screen(vt, vt->alt);
    vt->all_dirty = 1;
}

static void dec_mode(vt_t* vt, int mode, int on) {
    switch (mode) {
    case 1: vt->app_cursor = on; break;
    case 5: vt->reverse_video = on; vt->all_dirty = 1; break;
    case 6: vt->cur.origin = on; cursor_to(vt, 0, on ? vt->top : 0); break;
    case 7: vt->autowrap = on; break;
    case 25: vt->cursor_visible = on; dirty(vt, vt->cur.y); break;
    case 9: case 1000: vt->mouse = on ? VT_MOUSE_CLICK : VT_MOUSE_OFF; break;
    case 1002: vt->mouse = on ? VT_MOUSE_DRAG : VT_MOUSE_OFF; break;
    case 1003: vt->mouse = on ? VT_MOUSE_ANY : VT_MOUSE_OFF; break;
    case 1004: vt->focus_events = on; break;
    case 1006: vt->mouse_sgr = on; break;
    case 47: case 1047:
        set_alt(vt, on, mode == 1047);
        break;
    case 1048: if (on) save_cursor(vt); else restore_cursor(vt); break;
    case 1049:
        if (on) { save_cursor(vt); set_alt(vt, 1, 1); vt->cur = vt->saved_pri; }
        else { set_alt(vt, 0, 0); restore_cursor(vt); }
        break;
    case 2004: vt->paste_bracket = on; break;
    }
}

static void csi(vt_t* vt, uint8_t f) {
    vt_cursor_t* c = &vt->cur;
    if (vt->priv == '?') {
        if (f == 'h' || f == 'l') for (int i = 0; i < vt->nparams; i++) dec_mode(vt, vt->params[i], f == 'h');
        else if (f == 'J' || f == 'K') { vt->priv = 0; csi(vt, f); }       /* DECSED / DECSEL: as ED / EL */
        else if (f == 'n' && P(vt, 0, 0) == 6) {                          /* DECXCPR */
            char b[32];
            snprintf(b, sizeof(b), "\x1b[?%d;%dR", c->y + 1, c->x + 1);
            reply(vt, b);
        }
        return;
    }
    if (vt->priv == '>') {
        if (f == 'c') reply(vt, "\x1b[>41;380;0c");                      /* DA2: an xterm */
        return;
    }
    if (vt->priv) return;
    if (vt->inter == '!' && f == 'p') {                                  /* DECSTR: a soft reset */
        vt->insert = 0; vt->cur.origin = 0; vt->autowrap = 1; vt->cursor_visible = 1;
        vt->app_cursor = vt->app_keypad = 0; vt->top = 0; vt->bot = vt->rows - 1;
        vt->cur.attr = 0; vt->cur.fg = vt->cur.bg = VT_DEFAULT;
        return;
    }
    if (vt->inter == ' ' && f == 'q') return;                           /* DECSCUSR: the cursor's shape (one here) */
    if (vt->inter) return;
    int top = c->origin ? vt->top : 0;
    switch (f) {
    case 'A': {
        int y = c->y - P(vt, 0, 1);
        if (c->y >= vt->top && y < vt->top) y = vt->top;            /* (it stops at the region's top) */
        cursor_to(vt, c->x, y);
        break;
    }
    case 'B': case 'e': {
        int y = c->y + P(vt, 0, 1);
        if (c->y <= vt->bot && y > vt->bot) y = vt->bot;
        cursor_to(vt, c->x, y);
        break;
    }
    case 'C': case 'a': cursor_to(vt, c->x + P(vt, 0, 1), c->y); break;
    case 'D': cursor_to(vt, c->x - P(vt, 0, 1), c->y); break;
    case 'E': cursor_to(vt, 0, c->y + P(vt, 0, 1)); break;
    case 'F': cursor_to(vt, 0, c->y - P(vt, 0, 1)); break;
    case 'G': case '`': cursor_to(vt, P(vt, 0, 1) - 1, c->y); break;
    case 'H': case 'f': cursor_to(vt, P(vt, 1, 1) - 1, top + P(vt, 0, 1) - 1); break;
    case 'd': cursor_to(vt, c->x, top + P(vt, 0, 1) - 1); break;
    case 'I': for (int n = P(vt, 0, 1); n > 0; n--) control(vt, 9); break;
    case 'Z': {                                                          /* CBT */
        for (int n = P(vt, 0, 1); n > 0 && c->x > 0; n--) {
            int x = c->x - 1;
            while (x > 0 && !vt->tabs[x]) x--;
            c->x = x;
        }
        c->pending = 0;
        break;
    }
    case 'J': {
        int m = P(vt, 0, 0);
        if (m == 0) { erase_cells(vt, c->y, c->x, vt->cols); for (int y = c->y + 1; y < vt->rows; y++) erase_cells(vt, y, 0, vt->cols); }
        else if (m == 1) { for (int y = 0; y < c->y; y++) erase_cells(vt, y, 0, vt->cols); erase_cells(vt, c->y, 0, c->x + 1); }
        else if (m == 2) for (int y = 0; y < vt->rows; y++) erase_cells(vt, y, 0, vt->cols);
        else if (m == 3 && !vt->alt_on) {                                /* the scrollback too */
            for (int i = 0; i < vt->sb_count; i++) free(vt->sb[(vt->sb_head + i) % vt->sb_max].c);
            vt->sb_count = vt->sb_head = 0;
            vt->all_dirty = 1;
        }
        break;
    }
    case 'K': {
        int m = P(vt, 0, 0);
        if (m == 0) erase_cells(vt, c->y, c->x, vt->cols);
        else if (m == 1) erase_cells(vt, c->y, 0, c->x + 1);
        else erase_cells(vt, c->y, 0, vt->cols);
        break;
    }
    case 'X': erase_cells(vt, c->y, c->x, c->x + P(vt, 0, 1)); c->pending = 0; break;
    case 'L': if (c->y >= vt->top && c->y <= vt->bot) { scroll_down(vt, c->y, vt->bot, P(vt, 0, 1)); c->x = 0; c->pending = 0; } break;
    case 'M':
        if (c->y >= vt->top && c->y <= vt->bot) {
            int keep = vt->alt_on;
            vt->alt_on = 1;                                             /* (deleted lines do not go to the scrollback) */
            scroll_up(vt, c->y, vt->bot, P(vt, 0, 1));
            vt->alt_on = keep;
            c->x = 0;
            c->pending = 0;
        }
        break;
    case 'S': { int keep = vt->alt_on; if (vt->top) vt->alt_on = 1; scroll_up(vt, vt->top, vt->bot, P(vt, 0, 1)); vt->alt_on = keep; break; }
    case 'T': scroll_down(vt, vt->top, vt->bot, P(vt, 0, 1)); break;
    case '@': {
        int n = P(vt, 0, 1), x = c->x;
        if (n > vt->cols - x) n = vt->cols - x;
        vt_cell_t* l = vt->scr[c->y].c;
        memmove(&l[x + n], &l[x], sizeof(vt_cell_t) * (size_t)(vt->cols - x - n));
        blank(vt, &l[x], n);
        c->pending = 0;
        dirty(vt, c->y);
        break;
    }
    case 'P': {
        int n = P(vt, 0, 1), x = c->x;
        if (n > vt->cols - x) n = vt->cols - x;
        vt_cell_t* l = vt->scr[c->y].c;
        memmove(&l[x], &l[x + n], sizeof(vt_cell_t) * (size_t)(vt->cols - x - n));
        blank(vt, &l[vt->cols - n], n);
        c->pending = 0;
        dirty(vt, c->y);
        break;
    }
    case 'b': for (int n = P(vt, 0, 1); n > 0 && vt->last_ch; n--) put_char(vt, vt->last_ch); break;
    case 'm': sgr(vt); break;
    case 'r': {
        int t = P(vt, 0, 1) - 1, b = P(vt, 1, vt->rows) - 1;
        if (b >= vt->rows) b = vt->rows - 1;
        if (t < b) { vt->top = t; vt->bot = b; cursor_to(vt, 0, c->origin ? t : 0); }
        break;
    }
    case 's': save_cursor(vt); break;
    case 'u': restore_cursor(vt); break;
    case 'h': case 'l':
        for (int i = 0; i < vt->nparams; i++) {
            if (vt->params[i] == 4) vt->insert = f == 'h';
            else if (vt->params[i] == 20) vt->newline_mode = f == 'h';
        }
        break;
    case 'g':
        if (P(vt, 0, 0) == 0) vt->tabs[c->x] = 0;
        else if (P(vt, 0, 0) == 3) memset(vt->tabs, 0, (size_t)vt->cols);
        break;
    case 'n':
        if (P(vt, 0, 0) == 5) reply(vt, "\x1b[0n");
        else if (P(vt, 0, 0) == 6) {
            char b[32];
            snprintf(b, sizeof(b), "\x1b[%d;%dR", c->y - top + 1, c->x + 1);
            reply(vt, b);
        }
        break;
    case 'c': if (P(vt, 0, 0) == 0) reply(vt, "\x1b[?62;22c"); break;   /* DA: a VT220 with colours */
    case 't': {                                                         /* window reports */
        char b[48];
        if (P(vt, 0, 0) == 18) { snprintf(b, sizeof(b), "\x1b[8;%d;%dt", vt->rows, vt->cols); reply(vt, b); }
        break;
    }
    }
}

/* ── OSC, ESC ──────────────────────────────────────────────────────── */

static void osc(vt_t* vt) {
    vt->osc[vt->osc_len] = 0;
    int n = atoi(vt->osc);
    const char* t = strchr(vt->osc, ';');
    if (!t) return;
    t++;
    if (n == 0 || n == 2) {
        snprintf(vt->title, sizeof(vt->title), "%s", t);
        vt->title_changed = 1;
    } else if ((n == 10 || n == 11) && !strcmp(t, "?")) {               /* the colours asked */
        reply(vt, n == 10 ? "\x1b]10;rgb:e5e5/e5e5/e5e5\x1b\\" : "\x1b]11;rgb:1d1d/2323/2c2c\x1b\\");
    }
}

static void esc(vt_t* vt, uint8_t f) {
    if (vt->inter == '(' || vt->inter == ')') {
        int lines = f == '0';
        if (vt->inter == '(') vt->cur.g0_lines = lines; else vt->cur.g1_lines = lines;
        return;
    }
    if (vt->inter == '#' && f == '8') {                                 /* DECALN: the screen full of E */
        for (int y = 0; y < vt->rows; y++)
            for (int x = 0; x < vt->cols; x++) { vt->scr[y].c[x] = (vt_cell_t){ 'E', VT_DEFAULT, VT_DEFAULT, 0 }; }
        vt->all_dirty = 1;
        return;
    }
    if (vt->inter) return;
    switch (f) {
    case '7': save_cursor(vt); break;
    case '8': restore_cursor(vt); break;
    case 'D': index_down(vt); break;
    case 'E': vt->cur.x = 0; index_down(vt); break;
    case 'M': index_up(vt); break;
    case 'H': vt->tabs[vt->cur.x] = 1; break;
    case 'c': vt_reset(vt); break;
    case '=': vt->app_keypad = 1; break;
    case '>': vt->app_keypad = 0; break;
    }
}

/* ── the parser ────────────────────────────────────────────────────── */

static void byte(vt_t* vt, uint8_t b) {
    switch (vt->state) {
    case S_ESC:
        if (b == '[') { vt->state = S_CSI; vt->nparams = 0; vt->priv = 0; vt->inter = 0; memset(vt->sub, 0, sizeof(vt->sub)); vt->params[0] = 0; return; }
        if (b == ']') { vt->state = S_OSC; vt->osc_len = 0; return; }
        if (b == 'P' || b == '_' || b == '^' || b == 'X') { vt->state = S_STRING; return; }
        if (b >= 0x20 && b <= 0x2F) { vt->inter = (char)b; vt->state = S_ESC_INTER; return; }
        vt->state = S_GROUND;
        esc(vt, b);
        return;
    case S_ESC_INTER:
        if (b >= 0x20 && b <= 0x2F) return;
        vt->state = S_GROUND;
        esc(vt, b);
        return;
    case S_CSI:
        if (b >= '0' && b <= '9') {
            if (!vt->nparams) vt->nparams = 1;
            int* p = &vt->params[vt->nparams - 1];
            if (*p < 100000) *p = *p * 10 + (b - '0');
            return;
        }
        if (b == ';' || b == ':') {
            if (!vt->nparams) vt->nparams = 1;
            if (vt->nparams < (int)(sizeof(vt->params) / sizeof(vt->params[0]))) {
                vt->sub[vt->nparams] = b == ':';
                vt->params[vt->nparams++] = 0;
            }
            return;
        }
        if (b >= '<' && b <= '?') { vt->priv = (char)b; return; }
        if (b >= 0x20 && b <= 0x2F) { vt->inter = (char)b; return; }
        if (b >= 0x40 && b <= 0x7E) { vt->state = S_GROUND; csi(vt, b); return; }
        if (b < 0x20) { control(vt, b); return; }        /* (controls inside a sequence act) */
        vt->state = S_GROUND;
        return;
    case S_OSC:
        if (b == 7) { vt->state = S_GROUND; osc(vt); return; }
        if (b == 0x1B) { vt->state = S_OSC_ESC; return; }
        if (vt->osc_len < (int)sizeof(vt->osc) - 1) vt->osc[vt->osc_len++] = (char)b;
        return;
    case S_OSC_ESC:
        vt->state = S_GROUND;
        osc(vt);
        if (b != '\\') byte(vt, b);
        return;
    case S_STRING:                                      /* DCS, APC, PM, SOS: ignored */
        if (b == 7) vt->state = S_GROUND;
        else if (b == 0x1B) vt->state = S_STRING_ESC;
        return;
    case S_STRING_ESC:
        vt->state = b == '\\' ? S_GROUND : S_STRING;
        return;
    }
    /* ground */
    if (b == 0x1B) { vt->state = S_ESC; vt->inter = 0; vt->utf8_left = 0; return; }
    if (b < 0x20 || b == 0x7F) { if (b != 0x7F) control(vt, b); vt->utf8_left = 0; return; }
    if (b < 0x80) { vt->utf8_left = 0; put_char(vt, b); return; }
    /* UTF-8 */
    if (vt->utf8_left && (b & 0xC0) == 0x80) {
        vt->utf8_cp = vt->utf8_cp << 6 | (b & 0x3F);
        if (--vt->utf8_left == 0) put_char(vt, vt->utf8_cp);
        return;
    }
    if (vt->utf8_left) put_char(vt, 0xFFFD);            /* (a sequence cut short) */
    if (b >= 0xC2 && b <= 0xDF) { vt->utf8_cp = b & 0x1F; vt->utf8_left = 1; }
    else if (b >= 0xE0 && b <= 0xEF) { vt->utf8_cp = b & 0x0F; vt->utf8_left = 2; }
    else if (b >= 0xF0 && b <= 0xF4) { vt->utf8_cp = b & 0x07; vt->utf8_left = 3; }
    else { vt->utf8_left = 0; put_char(vt, 0xFFFD); }
}

void vt_write(vt_t* vt, const char* data, size_t n) {
    for (size_t i = 0; i < n; i++) byte(vt, (uint8_t)data[i]);
}

/* ── input ─────────────────────────────────────────────────────────── */

int vt_key_seq(const vt_t* vt, int key, int mods, char* out) {
    /* the modifier parameter of xterm: 1 + shift 1, alt 2, ctrl 4 */
    int m = 1 + ((mods & VT_MOD_SHIFT) ? 1 : 0) + ((mods & VT_MOD_ALT) ? 2 : 0) + ((mods & VT_MOD_CTRL) ? 4 : 0);
    const char* fin = NULL;
    int tilde = 0;
    switch (key) {
    case VT_KEY_UP: fin = "A"; break;
    case VT_KEY_DOWN: fin = "B"; break;
    case VT_KEY_RIGHT: fin = "C"; break;
    case VT_KEY_LEFT: fin = "D"; break;
    case VT_KEY_HOME: fin = "H"; break;
    case VT_KEY_END: fin = "F"; break;
    case VT_KEY_INSERT: tilde = 2; break;
    case VT_KEY_DELETE: tilde = 3; break;
    case VT_KEY_PGUP: tilde = 5; break;
    case VT_KEY_PGDN: tilde = 6; break;
    default:
        if (key >= VT_KEY_F1 && key < VT_KEY_F1 + 12) {
            static const int fk[12] = { 0, 0, 0, 0, 15, 17, 18, 19, 20, 21, 23, 24 };
            int i = key - VT_KEY_F1;
            if (i < 4) {
                if (m > 1) return sprintf(out, "\x1b[1;%d%c", m, "PQRS"[i]);
                return sprintf(out, "\x1bO%c", "PQRS"[i]);
            }
            tilde = fk[i];
        }
    }
    if (fin) {
        if (m > 1) return sprintf(out, "\x1b[1;%d%s", m, fin);
        return sprintf(out, "\x1b%c%s", vt->app_cursor ? 'O' : '[', fin);
    }
    if (tilde) {
        if (m > 1) return sprintf(out, "\x1b[%d;%d~", tilde, m);
        return sprintf(out, "\x1b[%d~", tilde);
    }
    return 0;
}

int vt_mouse_seq(const vt_t* vt, int button, int kind, int col, int row, int mods, char* out) {
    if (vt->mouse == VT_MOUSE_OFF) return 0;
    if (kind == VT_MOUSE_MOTION) {
        if (vt->mouse == VT_MOUSE_CLICK) return 0;
        if (vt->mouse == VT_MOUSE_DRAG && button < 0) return 0;      /* (no button held) */
    }
    if (kind == VT_MOUSE_RELEASE && button >= 64) return 0;           /* (wheels have no release) */
    int cb = button < 0 ? 3 : button;
    if (kind == VT_MOUSE_MOTION) cb += 32;
    if (mods & VT_MOD_SHIFT) cb += 4;
    if (mods & VT_MOD_ALT) cb += 8;
    if (mods & VT_MOD_CTRL) cb += 16;
    if (vt->mouse_sgr)
        return sprintf(out, "\x1b[<%d;%d;%d%c", cb, col + 1, row + 1, kind == VT_MOUSE_RELEASE ? 'm' : 'M');
    if (kind == VT_MOUSE_RELEASE) cb = (cb & ~3) | 3;                 /* (the old encoding: "a button went up") */
    if (col > 222) col = 222;
    if (row > 222) row = 222;
    return sprintf(out, "\x1b[M%c%c%c", 32 + cb, 33 + col, 33 + row);
}
