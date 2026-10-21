/* SPDX-License-Identifier: GPL-3.0-or-later */
/* apps/terminal/vt.c on Linux (run.sh): what programs write, and the cells
 * it makes. One line per check; the exit code is how many failed. */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include "vt.h"

static int g_fail, g_ok;

static void check(const char* what, int ok) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) g_ok++; else g_fail++;
}

static void w(vt_t* vt, const char* s) { vt_write(vt, s, strlen(s)); }

/* row y as text (wide characters' second halves left out, blanks as ' ', trailing ones cut) */
static const char* row(vt_t* vt, int y) {
    static char b[1024];
    const vt_line_t* l = vt_get_line(vt, y);
    int n = 0;
    if (!l) return "(none)";
    for (int x = 0; x < l->len; x++) {
        if (l->c[x].attr & VT_WIDE_CONT) continue;
        uint32_t c = l->c[x].ch ? l->c[x].ch : ' ';
        if (c < 0x80) b[n++] = (char)c;
        else if (c < 0x800) { b[n++] = (char)(0xC0 | c >> 6); b[n++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { b[n++] = (char)(0xE0 | c >> 12); b[n++] = (char)(0x80 | ((c >> 6) & 63)); b[n++] = (char)(0x80 | (c & 63)); }
        else { b[n++] = (char)(0xF0 | c >> 18); b[n++] = (char)(0x80 | ((c >> 12) & 63)); b[n++] = (char)(0x80 | ((c >> 6) & 63)); b[n++] = (char)(0x80 | (c & 63)); }
    }
    while (n && b[n - 1] == ' ') n--;
    b[n] = 0;
    return b;
}

static int is_row(vt_t* vt, int y, const char* want) { return !strcmp(row(vt, y), want); }

int main(void) {
    setlocale(LC_ALL, "C.UTF-8");
    printf("terminal: vt.c\n");

    vt_t* vt = vt_new(10, 5, 100);
    w(vt, "hello\r\nworld");
    check("text, CR LF; the cursor after it", is_row(vt, 0, "hello") && is_row(vt, 1, "world") && vt->cur.x == 5 && vt->cur.y == 1);

    vt_reset(vt);
    w(vt, "0123456789AB");
    check("autowrap: the 11th character on the next line, the line marked wrapped",
          is_row(vt, 0, "0123456789") && is_row(vt, 1, "AB") && vt->scr[0].wrapped);

    vt_reset(vt);
    w(vt, "0123456789\r\nx");
    check("the last column: no wrap until the next character (CR LF: no blank line)", is_row(vt, 1, "x") && is_row(vt, 2, ""));

    vt_reset(vt);
    w(vt, "\x1b[2;3HX\x1b[1;1H\x1b[5CY\x1b[3;1Habc\x1b[2D\x1b[KZ");
    check("CUP, CUF, CUB, EL", is_row(vt, 1, "  X") && is_row(vt, 0, "     Y") && is_row(vt, 2, "aZ"));

    vt_reset(vt);
    w(vt, "\x1b[31;1mR\x1b[38;5;123mP\x1b[38;2;1;2;3mT\x1b[48:2::9:8:7mU\x1b[0mn\x1b[7mv");
    const vt_cell_t* c = vt->scr[0].c;
    check("SGR: 16 colours + bold, 256, 24-bit (; and : forms), reset, reverse",
          c[0].fg == (VT_PAL | 1) && (c[0].attr & VT_BOLD) && c[1].fg == (VT_PAL | 123) && c[2].fg == (VT_RGB | 0x010203)
          && c[3].bg == (VT_RGB | 0x090807) && c[4].fg == VT_DEFAULT && !c[4].attr && (c[5].attr & VT_REVERSE));
    check("the palette: 256-colour cube and grey ramp", vt_rgb(VT_PAL | 196, 0) == 0xFF0000 && vt_rgb(VT_PAL | 232, 0) == 0x080808
          && vt_rgb(VT_DEFAULT, 0x123456) == 0x123456);
    vt_free(vt);

    vt = vt_new(10, 5, 100);
    for (int i = 0; i < 12; i++) { char b[16]; snprintf(b, sizeof(b), "L%d\r\n", i); w(vt, b); }
    check("scrollback: lines that leave the top", vt->sb_count == 8 && !strcmp(row(vt, -1), "L7") && !strcmp(row(vt, -8), "L0")
          && is_row(vt, 0, "L8") && is_row(vt, 3, "L11"));

    vt_reset(vt);
    int sb = vt->sb_count;
    w(vt, "a\r\nb\r\nc\r\nd\r\ne\x1b[2;4r\x1b[4;1H\n\n");
    check("a scrolling region (2-4): only it moves, nothing into the scrollback",
          is_row(vt, 0, "a") && is_row(vt, 1, "d") && is_row(vt, 2, "") && is_row(vt, 3, "") && is_row(vt, 4, "e") && vt->sb_count == sb);
    vt_reset(vt);
    w(vt, "1\r\n2\r\n3\r\n4\r\n5\x1b[2;1H\x1b[L\x1b[4;1H\x1b[M");
    check("IL / DL", is_row(vt, 0, "1") && is_row(vt, 1, "") && is_row(vt, 2, "2") && is_row(vt, 3, "4") && is_row(vt, 4, "")
          && vt->sb_count == sb);

    vt_reset(vt);
    w(vt, "abcdef\x1b[1;2H\x1b[2@\x1b[1;6H\x1b[P");
    check("ICH / DCH", is_row(vt, 0, "a  bcef"));
    w(vt, "\x1b[4h\x1b[1;1HXY\x1b[4l");
    check("insert mode", is_row(vt, 0, "XYa  bcef"));
    w(vt, "\x1b[1;1H\x1b[3X");
    check("ECH", is_row(vt, 0, "     bcef"));

    vt_reset(vt);
    w(vt, "keep\x1b[?1049h\x1b[1;1Halt");
    int on = vt->alt_on && is_row(vt, 0, "alt");
    w(vt, "\x1b[?1049l");
    check("the alternate screen (1049): its own, then the main one back with its cursor",
          on && !vt->alt_on && is_row(vt, 0, "keep") && vt->cur.x == 4 && vt->cur.y == 0);

    vt_reset(vt);
    w(vt, "\xe4\xb8\xad" "a\xf0\x9f\x98\x80" "b");
    c = vt->scr[0].c;
    check("double-width characters: two cells each", c[0].ch == 0x4E2D && (c[0].attr & VT_WIDE) && (c[1].attr & VT_WIDE_CONT)
          && c[2].ch == 'a' && c[3].ch == 0x1F600 && c[5].ch == 'b' && vt->cur.x == 6);
    vt_reset(vt);
    w(vt, "123456789\xe4\xb8\xad");
    check("a wide character at the last column goes to the next line", is_row(vt, 0, "123456789") && vt->scr[1].c[0].ch == 0x4E2D);
    vt_reset(vt);
    vt_write(vt, "\xc3", 1);
    vt_write(vt, "\xa9", 1);
    check("UTF-8 split between two writes", vt->scr[0].c[0].ch == 0xE9);

    vt_reset(vt);
    w(vt, "\x1b[3;4H\x1b[6n\x1b[c");
    check("DSR (the cursor's place) and DA answered", vt->reply_len > 0 && !strncmp(vt->reply, "\x1b[3;4R\x1b[?62;22c", (size_t)vt->reply_len));
    vt->reply_len = 0;

    w(vt, "\x1b]0;one\x07");
    int t1 = !strcmp(vt->title, "one");
    w(vt, "\x1b]2;two words\x1b\\x");
    check("OSC titles, BEL and ST ends", t1 && !strcmp(vt->title, "two words") && vt->title_changed && vt->scr[2].c[3].ch == 'x');

    vt_reset(vt);
    w(vt, "\x1b(0lqqk\x1b(Bq");
    check("DEC line drawing (ESC ( 0)", vt->scr[0].c[0].ch == 0x250C && vt->scr[0].c[1].ch == 0x2500 && vt->scr[0].c[3].ch == 0x2510 && vt->scr[0].c[4].ch == 'q');

    vt_reset(vt);
    w(vt, "a\tb\x1b[3g\x1b[1;4H\x1bH\r\tc");
    check("tab stops: default every 8, TBC, HTS", vt->scr[0].c[8].ch == 'b' && vt->scr[0].c[3].ch == 'c');

    vt_reset(vt);
    w(vt, "\x1b[44m\x1b[2J");
    check("erasing takes the background colour (bce)", vt->scr[3].c[5].bg == (VT_PAL | 4));
    vt_free(vt);

    vt = vt_new(10, 6, 100);
    w(vt, "1\r\n2\r\n3\r\n4\r\n5\r\n6");
    vt_resize(vt, 8, 3);
    check("fewer rows: the top lines go into the scrollback, the cursor stays on its line",
          is_row(vt, 0, "4") && is_row(vt, 2, "6") && vt->cur.y == 2 && vt->sb_count == 3 && !strcmp(row(vt, -1), "3"));
    vt_resize(vt, 20, 8);
    check("more rows and columns: blank ones added", vt->cols == 20 && vt->rows == 8 && is_row(vt, 2, "6") && is_row(vt, 7, ""));
    vt_reset(vt);
    w(vt, "ab\r\n");
    vt_resize(vt, 20, 2);
    check("fewer rows with blank ones below the cursor: those go first", is_row(vt, 0, "ab") && vt->cur.y == 1);

    char k[32];
    int n = vt_key_seq(vt, VT_KEY_UP, 0, k);
    int ok = n == 3 && !memcmp(k, "\x1b[A", 3);
    w(vt, "\x1b[?1h");
    n = vt_key_seq(vt, VT_KEY_UP, 0, k);
    ok = ok && n == 3 && !memcmp(k, "\x1bOA", 3);
    n = vt_key_seq(vt, VT_KEY_RIGHT, VT_MOD_CTRL, k);
    ok = ok && !strcmp(k, "\x1b[1;5C");
    n = vt_key_seq(vt, VT_KEY_DELETE, 0, k);
    ok = ok && !strcmp(k, "\x1b[3~");
    n = vt_key_seq(vt, VT_KEY_F1 + 4, 0, k);
    ok = ok && !strcmp(k, "\x1b[15~");
    check("keys: arrows (normal and application mode), Ctrl+Right, Delete, F5", ok);

    n = vt_mouse_seq(vt, 0, VT_MOUSE_PRESS, 2, 3, 0, k);
    ok = n == 0;
    w(vt, "\x1b[?1000h\x1b[?1006h");
    n = vt_mouse_seq(vt, 0, VT_MOUSE_PRESS, 2, 3, 0, k);
    ok = ok && !strcmp(k, "\x1b[<0;3;4M");
    vt_mouse_seq(vt, 0, VT_MOUSE_RELEASE, 2, 3, 0, k);
    ok = ok && !strcmp(k, "\x1b[<0;3;4m");
    ok = ok && vt_mouse_seq(vt, 0, VT_MOUSE_MOTION, 2, 3, 0, k) == 0;
    w(vt, "\x1b[?1006l");
    vt_mouse_seq(vt, 65, VT_MOUSE_PRESS, 0, 0, 0, k);
    ok = ok && !strcmp(k, "\x1b[Ma!!");
    check("mouse reporting: off by default, SGR press / release, no motion in mode 1000, the wheel (X10)", ok);

    vt_reset(vt);
    w(vt, "\x1b[5;5H\x1b[s\x1b[1;1H\x1b[u*\x1b" "7\x1b[2;2H\x1b" "8+");
    check("save / restore the cursor (CSI s / u, ESC 7 / 8)", vt->scr[1].c[4].ch == '*' && vt->scr[1].c[5].ch == '+');
    vt_free(vt);

    vt = vt_new(80, 24, 50);
    for (int i = 0; i < 2000; i++) w(vt, "\x1b[38;5;200mabcdefghijklmnopqrstuvwxyz\x1b[0m 0123456789 \xe4\xb8\xad\xe6\x96\x87\r\n");
    check("2000 lines through a 50-line scrollback", vt->sb_count == 50 && vt->cur.y == 23);
    vt_free(vt);

    printf("terminal vt: %d passed, %d failed\n", g_ok, g_fail);
    return g_fail;
}
