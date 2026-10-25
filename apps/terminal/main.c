/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Terminal: windows with tabs, each tab a pseudo-terminal running
 * the selected shell (or terminal -e command...). The screen's escape sequences are
 * vt.c's; here: the windows, drawing the cells, the keyboard and the mouse,
 * selection and the clipboard, the scrollback, resizing.
 *
 * Keys of its own: Ctrl+Shift+T a new tab, Ctrl+Shift+N a new window,
 * Ctrl+Shift+W closes the tab, Ctrl+PgUp / PgDn the tab before / after,
 * Ctrl+Shift+C / V copy / paste, Shift+PgUp / PgDn the scrollback,
 * Ctrl+= / Ctrl+- / Ctrl+0 the text size. The mouse selects (and copies);
 * the middle or right button pastes; the wheel scrolls.
 */
#include <banana.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "vt.h"
#include "prefs.h"

#define SCROLLBACK  5000
#define MAX_WINDOWS 16
#define TAB_H       28
#define PAD         4
#define DEF_FG      g_prefs.fg
#define DEF_BG      g_prefs.bg
#define CURSOR_C    g_prefs.cursor_color
#define SEL_BG      0x3A7BD5u
#define BAR_BG      0x14181Eu
#define TAB_BG      0x262D38u

/* the termcap entry of xterm-256color: bash's readline reads $TERMCAP */
static const char TERMCAP_ENTRY[] =
    "xterm-256color|xterm with 256 colors:am:bs:km:mi:ms:xn:ut:co#80:it#8:li#24:Co#256:pa#32767:"
    "AB=\\E[48;5;%dm:AF=\\E[38;5;%dm:"
    "AL=\\E[%dL:DC=\\E[%dP:DL=\\E[%dM:DO=\\E[%dB:IC=\\E[%d@:K1=\\EOw:K2=\\EOu:K3=\\EOy:K4=\\EOq:K5=\\EOs:"
    "LE=\\E[%dD:RI=\\E[%dC:SF=\\E[%dS:SR=\\E[%dT:UP=\\E[%dA:ae=\\E(B:al=\\E[L:as=\\E(0:bl=^G:bt=\\E[Z:"
    "cd=\\E[J:ce=\\E[K:cl=\\E[H\\E[2J:cm=\\E[%i%d;%dH:cr=\\r:cs=\\E[%i%d;%dr:ct=\\E[3g:dc=\\E[P:dl=\\E[M:"
    "do=\\n:ec=\\E[%dX:ei=\\E[4l:ho=\\E[H:im=\\E[4h:k1=\\EOP:k2=\\EOQ:k3=\\EOR:k4=\\EOS:k5=\\E[15~:"
    "k6=\\E[17~:k7=\\E[18~:k8=\\E[19~:k9=\\E[20~:kD=\\E[3~:kI=\\E[2~:kN=\\E[6~:kP=\\E[5~:kb=\\177:"
    "kd=\\EOB:ke=\\E[?1l\\E>:kh=\\EOH:@7=\\EOF:kl=\\EOD:kr=\\EOC:ks=\\E[?1h\\E=:ku=\\EOA:le=^H:mb=\\E[5m:"
    "md=\\E[1m:me=\\E[0m:mh=\\E[2m:mr=\\E[7m:nd=\\E[C:nw=\\EE:rc=\\E8:sc=\\E7:se=\\E[27m:sf=\\n:"
    "so=\\E[7m:sr=\\EM:st=\\EH:ta=^I:te=\\E[?1049l:ti=\\E[?1049h:ue=\\E[24m:up=\\E[A:us=\\E[4m:"
    "vb=\\E[?5h\\E[?5l:ve=\\E[?12l\\E[?25h:vi=\\E[?25l:vs=\\E[?12;25h:";

typedef struct { long line; int col; } spot_t;          /* a cell: line counted since the start (vt->scrolled + index) */

typedef struct {
    vt_t*  vt;
    int    fd;                  /* the pty's master; -1 once closed */
    pid_t  pid;                 /* the program; 0 once it ended */
    int    view;                /* lines scrolled back (0: the screen) */
    char   title[128];
    char*  out;                 /* typed / pasted, not yet taken by the pty */
    size_t out_len, out_cap;
    int    status;              /* the program's wait status, once it ended */
    int    dead;                /* it ended badly: the tab stays, saying so, until a key */
} tab_t;

typedef struct {
    bwin_t win;
    int    open;
    tab_t** tabs;
    int    ntabs, cur;
    int    dirty, bar_dirty;
    int    focused;
    unsigned last_draw;
    int    selecting, has_sel;  /* a selection being made / one there */
    spot_t sel_a, sel_b;
    unsigned last_click;
    spot_t last_click_at;
    int    mouse_btn;           /* the button the program was told is held (-1 none) */
    int    mx, my;              /* where the mouse was last (the wheel's events do not say) */
    char   shown_title[160];
} term_t;

static term_t g_w[MAX_WINDOWS];
static term_prefs_t g_prefs;
static bwin_t g_preferences={.id=-1};
static char g_pref_status[128];
static int g_pref_edit=-1,g_pref_replace;
static char g_pref_hex[7];
static void preferences_open(void);
static void preferences_draw(void);
static void preferences_event(const banana_event_t*);
static int g_font = 15, g_cw = 9, g_ch = 18, g_ascent = 14;
static char** g_cmd;            /* terminal -e command...: what tabs run (NULL: the selected shell) */

/* ── glyphs: each drawn once by the system's font, kept as coverage ── */

#define GCACHE 4096
typedef struct { uint32_t key; uint8_t* a; } glyph_t;
static glyph_t g_glyph[GCACHE];
static int g_nglyph;

static void glyph_flush(void) {
    for (int i = 0; i < GCACHE; i++) { free(g_glyph[i].a); g_glyph[i].a = NULL; g_glyph[i].key = 0; }
    g_nglyph = 0;
}

/* the coverage (0..255) of character cp, 2 * g_cw wide (wide ones use both halves) */
static const uint8_t* glyph(uint32_t cp, int bold) {
    uint32_t key = (cp << 1 | (uint32_t)bold) + 1;
    unsigned h = (key * 2654435761u) % GCACHE;
    while (g_glyph[h].a) {
        if (g_glyph[h].key == key) return g_glyph[h].a;
        h = (h + 1) % GCACHE;
    }
    if (g_nglyph > GCACHE * 3 / 4) {                      /* (full: start again) */
        glyph_flush();
        h = (key * 2654435761u) % GCACHE;
    }
    int gw = g_cw * 2;
    static unsigned int* tmp;
    static int tmp_n;
    if (tmp_n < gw * g_ch) { free(tmp); tmp = malloc(sizeof(unsigned int) * (size_t)(gw * g_ch)); tmp_n = tmp ? gw * g_ch : 0; }
    uint8_t* a = malloc((size_t)(gw * g_ch));
    if (!tmp || !a) { free(a); return NULL; }
    memset(tmp, 0, sizeof(unsigned int) * (size_t)(gw * g_ch));
    char s[8];
    int n = 0;
    if (cp < 0x80) s[n++] = (char)cp;
    else if (cp < 0x800) { s[n++] = (char)(0xC0 | cp >> 6); s[n++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { s[n++] = (char)(0xE0 | cp >> 12); s[n++] = (char)(0x80 | ((cp >> 6) & 63)); s[n++] = (char)(0x80 | (cp & 63)); }
    else { s[n++] = (char)(0xF0 | cp >> 18); s[n++] = (char)(0x80 | ((cp >> 12) & 63)); s[n++] = (char)(0x80 | ((cp >> 6) & 63)); s[n++] = (char)(0x80 | (cp & 63)); }
    s[n] = 0;
    bwin_t fake = { 0, gw, g_ch, tmp };
    bwin_font(&fake, 0, 0, bold ? BANANA_FONT_MONO_BOLD : BANANA_FONT_MONO, g_font, s, 0xFFFFFF);
    for (int i = 0; i < gw * g_ch; i++) a[i] = (uint8_t)(tmp[i] >> 8);
    g_glyph[h].key = key;
    g_glyph[h].a = a;
    g_nglyph++;
    return a;
}

static void set_font(int size) {
    if (size < 8) size = 8;
    if (size > 40) size = 40;
    g_font = size;
    int asc = 0, desc = 0, lh = 0;
    banana_font_metrics(BANANA_FONT_MONO, size, &asc, &desc, &lh);
    g_cw = banana_font_width(BANANA_FONT_MONO, size, "M");
    if (g_cw < 4) g_cw = 4;
    g_ch = lh > asc + desc ? lh : asc + desc + 1;
    g_ascent = asc;
    glyph_flush();
}

static inline uint32_t mix(uint32_t bg, uint32_t fg, unsigned a) {
    if (a == 0) return bg;
    if (a == 255) return fg;
    uint32_t r = ((bg >> 16 & 255) * (255 - a) + (fg >> 16 & 255) * a) / 255;
    uint32_t g = ((bg >> 8 & 255) * (255 - a) + (fg >> 8 & 255) * a) / 255;
    uint32_t b = ((bg & 255) * (255 - a) + (fg & 255) * a) / 255;
    return r << 16 | g << 8 | b;
}

/* ── tabs: a program on a pty each ──────────────────────────────────── */

static void text_area(const term_t* t, int* cols, int* rows) {
    int w = t->win.w - 2 * PAD, h = t->win.h - TAB_H - 2 * PAD;
    *cols = w / g_cw > 1 ? w / g_cw : 2;
    *rows = h / g_ch > 1 ? h / g_ch : 2;
}

static void child_env(void) {
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    setenv("TERM_PROGRAM", "banana-terminal", 1);
    setenv("TERMCAP", TERMCAP_ENTRY, 1);
    setenv("LANG", "C.UTF-8", 0);
    /* (the shells' prompts: their own rc files - /apps/bash/bashrc, /apps/zsh/zshrc) */
    unsetenv("COLUMNS");
    unsetenv("LINES");
}

/* Read the saved choice for each new tab, so chsh takes effect without
 * replacing the terminal emulator or disturbing existing shells. */
static const char* default_shell(void) {
    if(g_prefs.shell){const char* chosen=g_prefs.shell==2?"/bin/zsh":"/bin/bash";return access(chosen,X_OK)==0?chosen:"/bin/bash";}
    char line[128];
    FILE* f = fopen("/etc/settings.conf", "r");
    const char* path = "/bin/bash";
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            char* p = line; while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "shell=", 6)) continue;
            p += 6; p[strcspn(p, "\r\n")] = 0;
            size_t n = strlen(p); while (n && (p[n-1] == ' ' || p[n-1] == '\t')) p[--n] = 0;
            if (!strcmp(p, "zsh")) path = "/bin/zsh";
            else if (!strcmp(p, "sh")) path = "/bin/sh";
            break;
        }
        fclose(f);
    }
    return access(path, X_OK) == 0 ? path : "/bin/bash";
}

static tab_t* tab_new(term_t* t) {
    int cols, rows;
    text_area(t, &cols, &rows);
    tab_t* tab = calloc(1, sizeof(tab_t));
    if (!tab) return NULL;
    tab->vt = vt_new(cols, rows, SCROLLBACK);
    if (!tab->vt) { free(tab); return NULL; }
    const char* shell = default_shell();
    snprintf(tab->title, sizeof(tab->title), "%s", g_cmd ? g_cmd[0] : strrchr(shell, '/') + 1);
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    int m = -1;
    pid_t pid = forkpty(&m, NULL, NULL, &ws);
    if (pid == 0) {
        child_env();
        setenv("SHELL", shell, 1);
        const char* home = getenv("HOME");
        if (home && !g_cmd) chdir(home);
        if (g_cmd) execvp(g_cmd[0], g_cmd);
        else execl(shell, strrchr(shell, '/') + 1, (char*)NULL);
        printf("terminal: cannot run %s: %s\r\n", g_cmd ? g_cmd[0] : shell, strerror(errno));
        if (!g_cmd) printf("(choose an installed shell with chsh bash or chsh zsh)\r\n");
        _exit(127);
    }
    if (pid < 0) {
        const char* msg = "terminal: cannot start a program here (fork needs the 64-bit Banana OS)\r\n";
        vt_write(tab->vt, msg, strlen(msg));
        tab->fd = -1;
        tab->pid = 0;
        tab->dead = 1;
    } else {
        tab->fd = m;
        tab->pid = pid;
        fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
    }
    return tab;
}

static void tab_free(tab_t* tab) {
    if (tab->fd >= 0) close(tab->fd);                   /* (the program gets SIGHUP) */
    vt_free(tab->vt);
    free(tab->out);
    free(tab);
}

/* bytes for the program (kept when the pty cannot take them yet) */
static void tab_send(tab_t* tab, const char* s, size_t n) {
    if (tab->fd < 0 || !n) return;
    if (!tab->out_len) {
        ssize_t k = write(tab->fd, s, n);
        if (k > 0) { s += k; n -= (size_t)k; }
        if (!n) return;
    }
    if (tab->out_len + n > tab->out_cap) {
        size_t cap = (tab->out_len + n) * 2;
        char* b = realloc(tab->out, cap);
        if (!b) return;
        tab->out = b;
        tab->out_cap = cap;
    }
    memcpy(tab->out + tab->out_len, s, n);
    tab->out_len += n;
}

static void tab_flush(tab_t* tab) {
    if (tab->fd < 0 || !tab->out_len) return;
    ssize_t k = write(tab->fd, tab->out, tab->out_len);
    if (k > 0) {
        memmove(tab->out, tab->out + k, tab->out_len - (size_t)k);
        tab->out_len -= (size_t)k;
    }
}

static term_t* win_new(void);

static void win_close(term_t* t) {
    for (int i = 0; i < t->ntabs; i++) tab_free(t->tabs[i]);
    free(t->tabs);
    t->tabs = NULL;
    t->ntabs = 0;
    bwin_close(&t->win);
    t->open = 0;
}

static void add_tab(term_t* t) {
    tab_t* tab = tab_new(t);
    if (!tab) return;
    tab_t** a = realloc(t->tabs, sizeof(tab_t*) * (size_t)(t->ntabs + 1));
    if (!a) { tab_free(tab); return; }
    t->tabs = a;
    t->tabs[t->ntabs] = tab;
    t->cur = t->ntabs++;
    t->has_sel = t->selecting = 0;
    t->dirty = t->bar_dirty = 1;
    t->tabs[t->cur]->vt->all_dirty = 1;
}

static void close_tab(term_t* t, int i) {
    if (i < 0 || i >= t->ntabs) return;
    tab_free(t->tabs[i]);
    memmove(&t->tabs[i], &t->tabs[i + 1], sizeof(tab_t*) * (size_t)(t->ntabs - i - 1));
    t->ntabs--;
    if (!t->ntabs) { win_close(t); return; }
    if (t->cur >= t->ntabs) t->cur = t->ntabs - 1;
    else if (t->cur > i) t->cur--;
    t->has_sel = t->selecting = 0;
    t->dirty = t->bar_dirty = 1;
    t->tabs[t->cur]->vt->all_dirty = 1;
}

static void switch_tab(term_t* t, int i) {
    if (i < 0 || i >= t->ntabs || i == t->cur) return;
    t->cur = i;
    t->has_sel = t->selecting = 0;
    t->dirty = t->bar_dirty = 1;
    t->tabs[i]->vt->all_dirty = 1;
}

static term_t* win_new(void) {
    term_t* t = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) if (!g_w[i].open) { t = &g_w[i]; break; }
    if (!t) return NULL;
    memset(t, 0, sizeof(*t));
    int w = 2 * PAD + 80 * g_cw, h = TAB_H + 2 * PAD + 24 * g_ch;
    if (bwin_open(&t->win, "Terminal", w, h) != 0) return NULL;
    bwin_resizable(&t->win, 2 * PAD + 20 * g_cw, TAB_H + 2 * PAD + 4 * g_ch);
    bwin_wheel(&t->win);
    t->open = 1;
    t->focused = 1;
    t->mouse_btn = -1;
    add_tab(t);
    if (!t->ntabs) { win_close(t); return NULL; }
    return t;
}

static void resize_tabs(term_t* t) {
    int cols, rows;
    text_area(t, &cols, &rows);
    for (int i = 0; i < t->ntabs; i++) {
        tab_t* tab = t->tabs[i];
        if (tab->vt->cols == cols && tab->vt->rows == rows) continue;
        vt_resize(tab->vt, cols, rows);
        tab->view = 0;
        if (tab->fd >= 0) {
            struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
            ioctl(tab->fd, TIOCSWINSZ, &ws);
        }
    }
    t->has_sel = t->selecting = 0;
    t->dirty = t->bar_dirty = 1;
}

/* ── drawing ───────────────────────────────────────────────────────── */

static int spot_cmp(spot_t a, spot_t b) { return a.line != b.line ? (a.line < b.line ? -1 : 1) : a.col - b.col; }

static int selected(const term_t* t, long line, int col) {
    if (!t->has_sel) return 0;
    spot_t a = t->sel_a, b = t->sel_b, s = { line, col };
    if (spot_cmp(a, b) > 0) { spot_t k = a; a = b; b = k; }
    return spot_cmp(s, a) >= 0 && spot_cmp(s, b) <= 0;
}

static void fill(bwin_t* w, int x, int y, int cw, int ch, uint32_t c) {
    if (x < 0 || y < 0 || x + cw > w->w || y + ch > w->h) {
        bwin_fill_rect(w, x, y, cw, ch, c);
        return;
    }
    for (int yy = 0; yy < ch; yy++) {
        unsigned int* p = w->px + (y + yy) * w->w + x;
        for (int xx = 0; xx < cw; xx++) p[xx] = c;
    }
}

static void draw_row(term_t* t, int r) {
    tab_t* tab = t->tabs[t->cur];
    vt_t* vt = tab->vt;
    bwin_t* w = &t->win;
    int li = r - tab->view;
    const vt_line_t* L = vt_get_line(vt, li);
    long abs_line = vt->scrolled + li;
    int y0 = TAB_H + PAD + r * g_ch;
    if (y0 + g_ch > w->h) return;
    /* (the margin right of the last column, in the default background) */
    int right = PAD + vt->cols * g_cw;
    fill(w, right, y0, w->w - right, g_ch, DEF_BG);
    int cur_here = tab->view == 0 && r == vt->cur.y && vt->cursor_visible;
    for (int x = 0; x < vt->cols; x++) {
        vt_cell_t c = { 0, VT_DEFAULT, VT_DEFAULT, 0 };
        if (L && x < L->len) c = L->c[x];
        if (c.attr & VT_WIDE_CONT) {
            /* (drawn with its first half, unless that one is off the line) */
            if (x > 0) continue;
            c.ch = 0;
        }
        int cells = (c.attr & VT_WIDE) && x + 1 < vt->cols ? 2 : 1;
        uint32_t fgc = c.fg;
        if ((c.attr & VT_BOLD) && (fgc & VT_PAL) && (fgc & 0xFF) < 8) fgc = VT_PAL | ((fgc & 0xFF) + 8);
        uint32_t fg = vt_rgb(fgc, DEF_FG), bg = vt_rgb(c.bg, DEF_BG);
        if (!!(c.attr & VT_REVERSE) != !!vt->reverse_video) { uint32_t k = fg; fg = bg; bg = k; }
        if (c.attr & VT_FAINT) fg = mix(bg, fg, 150);
        if (c.attr & VT_INVISIBLE) fg = bg;
        if (selected(t, abs_line, x)) { bg = SEL_BG; fg = 0xFFFFFF; }
        int is_cur = cur_here && (x == vt->cur.x || (cells == 2 && x + 1 == vt->cur.x));
        if (is_cur && t->focused && g_prefs.cursor==0) { fg = DEF_BG; bg = CURSOR_C; }
        int px = PAD + x * g_cw;
        fill(w, px, y0, cells * g_cw, g_ch, bg);
        if (c.ch > ' ' && c.ch != 0xA0) {
            const uint8_t* a = glyph(c.ch, (c.attr & VT_BOLD) != 0);
            if (a) {
                int gw = g_cw * 2, dw = cells * g_cw;
                for (int yy = 0; yy < g_ch; yy++) {
                    unsigned int* p = w->px + (y0 + yy) * w->w + px;
                    const uint8_t* s = a + yy * gw;
                    for (int xx = 0; xx < dw && px + xx < w->w; xx++) if (s[xx]) p[xx] = mix(p[xx], fg, s[xx]);
                }
            }
        }
        if (c.attr & VT_UNDERLINE) fill(w, px, y0 + g_ascent + 1, cells * g_cw, 1, fg);
        if (c.attr & VT_STRIKE) fill(w, px, y0 + g_ch / 2, cells * g_cw, 1, fg);
        if(is_cur&&t->focused&&g_prefs.cursor==1)fill(w,px,y0+g_ch-2,cells*g_cw,2,CURSOR_C);
        if(is_cur&&t->focused&&g_prefs.cursor==2)fill(w,px,y0,2,g_ch,CURSOR_C);
        if (is_cur && !t->focused) bwin_rect(w, px, y0, cells * g_cw, g_ch, CURSOR_C);
        if (cells == 2) x++;
    }
}

static void tab_label(const term_t* t, int i, int* x0, int* x1) {
    int avail = t->win.w - 2*TAB_H - 4;
    int tw = t->ntabs ? avail / t->ntabs : avail;
    if (tw > 220) tw = 220;
    *x0 = 2 + i * tw;
    *x1 = *x0 + tw - 2;
}

static void draw_bar(term_t* t) {
    bwin_t* w = &t->win;
    fill(w, 0, 0, w->w, TAB_H, BAR_BG);
    int asc = 0, desc = 0, lh = 0;
    banana_font_metrics(BANANA_FONT_SANS, 13, &asc, &desc, &lh);
    int ty = (TAB_H - lh) / 2;
    for (int i = 0; i < t->ntabs; i++) {
        int x0, x1;
        tab_label(t, i, &x0, &x1);
        int active = i == t->cur;
        fill(w, x0, 3, x1 - x0, TAB_H - 3, active ? DEF_BG : TAB_BG);
        if (active) fill(w, x0, 3, x1 - x0, 2, CURSOR_C);
        /* the title, cut to fit before the close mark */
        char s[128];
        snprintf(s, sizeof(s), "%s", t->tabs[i]->title);
        int room = x1 - x0 - 28;
        while (s[0] && banana_font_width(BANANA_FONT_SANS, 13, s) > room) {
            size_t n = strlen(s);
            do n--; while (n && ((unsigned char)s[n] & 0xC0) == 0x80);
            s[n] = 0;
        }
        bwin_font(w, x0 + 8, ty + 2, BANANA_FONT_SANS, 13, s, active ? DEF_FG : 0xA8B0BC);
        bwin_font(w, x1 - 16, ty + 2, BANANA_FONT_SANS, 13, "\xc3\x97", 0x8890A0);        /* × */
    }
    /* Sliders: preferences, always available beside the new-tab button. */
    int gear=w->w-2*TAB_H+6;
    for(int j=0;j<3;j++){fill(w,gear,8+j*6,14,1,0xA8B0BC);fill(w,gear+3+(j%2)*6,6+j*6,3,5,0xE5E5E5);}
    /* + : a new tab */
    int px = t->win.w - TAB_H + 2;
    fill(w, px, 4, TAB_H - 6, TAB_H - 7, TAB_BG);
    bwin_font(w, px + (TAB_H - 6 - banana_font_width(BANANA_FONT_SANS_BOLD, 16, "+")) / 2, ty, BANANA_FONT_SANS_BOLD, 16, "+", 0xE5E5E5);
}

static void draw(term_t* t) {
    tab_t* tab = t->tabs[t->cur];
    vt_t* vt = tab->vt;
    int full = vt->all_dirty || t->dirty > 1;
    if (t->bar_dirty || full) draw_bar(t);
    if (full) {
        /* the padding round the text */
        fill(&t->win, 0, TAB_H, t->win.w, PAD, DEF_BG);
        fill(&t->win, 0, TAB_H, PAD, t->win.h - TAB_H, DEF_BG);
        int bottom = TAB_H + PAD + vt->rows * g_ch;
        if (bottom < t->win.h) fill(&t->win, 0, bottom, t->win.w, t->win.h - bottom, DEF_BG);
    }
    for (int r = 0; r < vt->rows; r++) if (full || vt->dirty[r]) draw_row(t, r);
    memset(vt->dirty, 0, (size_t)vt->rows);
    vt->all_dirty = 0;
    t->dirty = t->bar_dirty = 0;
    bwin_update(&t->win);
    /* the window's title: the tab's */
    char title[160];
    snprintf(title, sizeof(title), "%s - Terminal", tab->title);
    if (strcmp(title, t->shown_title)) {
        snprintf(t->shown_title, sizeof(t->shown_title), "%s", title);
        bwin_title(&t->win, title);
    }
}

/* ── saved terminal preferences ──────────────────────────────────── */
static const uint32_t pref_colors[]={0x1D232C,0x101010,0x002B36,0xF8FAFC,0xE5E5E5,0x93A1A1,0xF4D35E,0x2563EB,0x2AA198,0xF38BA8};
static void preferences_draw(void){
 bwin_t* w=&g_preferences;if(w->id<0)return;
 bwin_clear(w,0x171D27);
 bwin_font(w,20,15,BANANA_FONT_SANS_BOLD,18,"Terminal preferences",0xF1F5F9);
 const char* labels[]={"Theme","Text size","Background","Text color","Cursor style","Cursor color","Shell for new tabs"};
 char values[7][48];
 snprintf(values[0],48,"%s",g_prefs.theme<0?"custom":term_themes[g_prefs.theme]);
 snprintf(values[1],48,"%d px",g_prefs.font);
 snprintf(values[2],48,"#%06X",g_prefs.bg);snprintf(values[3],48,"#%06X",g_prefs.fg);
 snprintf(values[4],48,"%s",term_cursors[g_prefs.cursor]);
 snprintf(values[5],48,"#%06X",g_prefs.cursor_color);snprintf(values[6],48,"%s",term_shells[g_prefs.shell]);
 for(int i=0;i<7;i++){
  int y=52+i*36;
  bwin_font(w,20,y+7,BANANA_FONT_SANS,14,labels[i],0xC5CFDD);
  bwin_fill_rect(w,210,y,226,29,0x283345);
  bwin_font(w,219,y+6,BANANA_FONT_SANS,14,"<",0x93C5FD);
  if(i==g_pref_edit)snprintf(values[i],48,"#%s_",g_pref_hex);
  bwin_font(w,244,y+6,BANANA_FONT_SANS,14,values[i],i==g_pref_edit?0x93C5FD:0xF1F5F9);
  bwin_font(w,418,y+6,BANANA_FONT_SANS,14,">",0x93C5FD);
 }
 bwin_fill_rect(w,20,312,416,42,g_prefs.bg);
 bwin_font(w,30,321,BANANA_FONT_MONO,16,"banana@pc:~$ echo hello",g_prefs.fg);
 bwin_fill_rect(w,275,g_prefs.cursor==1?338:321,g_prefs.cursor==2?2:9,g_prefs.cursor==1?2:20,g_prefs.cursor_color);
 const char* note=g_pref_status[0]?g_pref_status:g_pref_edit>=0?"Six hex digits. Enter applies; Esc cancels.":"Saved automatically. Existing shells keep running.";
 bwin_font(w,20,368,BANANA_FONT_SANS,12,note,g_pref_status[0]?0xFDA4AF:0x94A3B8);
 bwin_update(w);
}
static void preferences_open(void){
 if(g_preferences.id>=0)return;
 if(bwin_open(&g_preferences,"Terminal preferences",456,400)<0)return;
 preferences_draw();
}
static void preferences_commit(term_prefs_t next){
 g_pref_status[0]=0;
 if(next.shell==2&&access("/bin/zsh",X_OK)<0){snprintf(g_pref_status,sizeof(g_pref_status),"Zsh is not installed");preferences_draw();return;}
 int resized=next.font!=g_font;
 g_prefs=next;if(resized)set_font(g_prefs.font);
 for(int i=0;i<MAX_WINDOWS;i++)if(g_w[i].open){if(resized)resize_tabs(&g_w[i]);g_w[i].dirty=2;g_w[i].bar_dirty=1;}
 if(term_prefs_save(&g_prefs)<0)snprintf(g_pref_status,sizeof(g_pref_status),"Could not save preferences");
 preferences_draw();
}
static void preferences_event(const banana_event_t* ev){
 if(ev->type==BANANA_EV_KEY&&g_pref_edit>=0){
  int k=ev->key;size_t n=strlen(g_pref_hex);
  if(k==27){g_pref_edit=-1;g_pref_status[0]=0;preferences_draw();return;}
  if(k=='\n'||k=='\r'){
   if(n!=6){snprintf(g_pref_status,sizeof(g_pref_status),"Enter six hexadecimal digits");preferences_draw();return;}
   term_prefs_t next=g_prefs;
   term_prefs_set(&next,g_pref_edit==2?"background":g_pref_edit==3?"foreground":"cursor_color",g_pref_hex);
   g_pref_edit=-1;preferences_commit(next);return;
  }
  if(k==8||k==127){if(g_pref_replace)g_pref_hex[0]=0;else if(n)g_pref_hex[n-1]=0;g_pref_replace=0;}
  else if((k>='0'&&k<='9')||(k>='a'&&k<='f')||(k>='A'&&k<='F')){
   if(g_pref_replace){n=0;g_pref_hex[0]=0;g_pref_replace=0;}
   if(n<6){g_pref_hex[n]=(char)k;g_pref_hex[n+1]=0;}
  }
  preferences_draw();return;
 }
 if(ev->type==BANANA_EV_CLOSE||(ev->type==BANANA_EV_KEY&&ev->key==27)){bwin_close(&g_preferences);g_preferences.id=-1;g_pref_edit=-1;return;}
 if(ev->type!=BANANA_EV_MOUSE_DOWN||ev->button!=1||ev->x<210||ev->x>=436||ev->y<52||ev->y>=304)return;
 int row=(ev->y-52)/36,dir=ev->x<239?-1:1;
 if((row==2||row==3||row==5)&&ev->x>=239&&ev->x<410){
  g_pref_edit=row;g_pref_replace=1;g_pref_status[0]=0;
  snprintf(g_pref_hex,sizeof(g_pref_hex),"%06X",row==2?g_prefs.bg:row==3?g_prefs.fg:g_prefs.cursor_color);
  preferences_draw();return;
 }
 g_pref_edit=-1;
 term_prefs_t next=g_prefs;
 if(row==0)term_prefs_set(&next,"theme",term_themes[((g_prefs.theme<0?(dir>0?-1:0):g_prefs.theme)+dir+4)%4]);
 else if(row==1){int f=next.font+dir;if(f>=8&&f<=40)next.font=f;}
 else if(row==4)next.cursor=(next.cursor+dir+3)%3;
 else if(row==6)next.shell=(next.shell+dir+3)%3;
 else{
  uint32_t* c=row==2?&next.bg:row==3?&next.fg:&next.cursor_color;
  int n=sizeof(pref_colors)/sizeof(pref_colors[0]),at=dir>0?-1:0;
  for(int i=0;i<n;i++)if(pref_colors[i]==*c){at=i;break;}
  *c=pref_colors[(at+dir+n)%n];if(row!=5)next.theme=-1;
 }
 preferences_commit(next);
}

/* ── selection and the clipboard ───────────────────────────────────── */

static void copy_selection(term_t* t) {
    if (!t->has_sel) return;
    vt_t* vt = t->tabs[t->cur]->vt;
    spot_t a = t->sel_a, b = t->sel_b;
    if (spot_cmp(a, b) > 0) { spot_t k = a; a = b; b = k; }
    size_t cap = 4096, n = 0;
    char* s = malloc(cap);
    if (!s) return;
    for (long line = a.line; line <= b.line; line++) {
        const vt_line_t* L = vt_get_line(vt, (int)(line - vt->scrolled));
        int x0 = line == a.line ? a.col : 0, x1 = line == b.line ? b.col : (L ? L->len - 1 : 0);
        size_t start = n;
        for (int x = x0; L && x <= x1 && x < L->len; x++) {
            uint32_t c = L->c[x].ch;
            if (L->c[x].attr & VT_WIDE_CONT) continue;
            if (!c) c = ' ';
            if (n + 8 > cap) { cap *= 2; char* k = realloc(s, cap); if (!k) { free(s); return; } s = k; }
            if (c < 0x80) s[n++] = (char)c;
            else if (c < 0x800) { s[n++] = (char)(0xC0 | c >> 6); s[n++] = (char)(0x80 | (c & 63)); }
            else if (c < 0x10000) { s[n++] = (char)(0xE0 | c >> 12); s[n++] = (char)(0x80 | ((c >> 6) & 63)); s[n++] = (char)(0x80 | (c & 63)); }
            else { s[n++] = (char)(0xF0 | c >> 18); s[n++] = (char)(0x80 | ((c >> 12) & 63)); s[n++] = (char)(0x80 | ((c >> 6) & 63)); s[n++] = (char)(0x80 | (c & 63)); }
        }
        while (n > start && s[n - 1] == ' ') n--;             /* (the blanks at a line's end) */
        if (line < b.line && !(L && L->wrapped)) {
            if (n + 2 > cap) { cap *= 2; char* k = realloc(s, cap); if (!k) { free(s); return; } s = k; }
            s[n++] = '\n';
        }
    }
    s[n] = 0;
    if (n) banana_copy(s);
    free(s);
}

static void paste(term_t* t) {
    const char* p = banana_paste();
    if (!p || !*p) return;
    tab_t* tab = t->tabs[t->cur];
    if (tab->vt->paste_bracket) tab_send(tab, "\x1b[200~", 6);
    /* (line ends as Enter sends them) */
    for (const char* s = p; *s;) {
        const char* e = s;
        while (*e && *e != '\n') e++;
        tab_send(tab, s, (size_t)(e - s));
        if (*e) { tab_send(tab, "\r", 1); e++; }
        s = e;
    }
    if (tab->vt->paste_bracket) tab_send(tab, "\x1b[201~", 6);
    tab->view = 0;
    t->dirty = 2;
}

/* the cell under (mx, my): its spot; 0 if outside the text */
static int cell_at(term_t* t, int mx, int my, int* col, int* row) {
    vt_t* vt = t->tabs[t->cur]->vt;
    int c = (mx - PAD) / g_cw, r = (my - TAB_H - PAD) / g_ch;
    if (mx < PAD) c = 0;
    if (my < TAB_H + PAD) r = 0;
    if (c >= vt->cols) c = vt->cols - 1;
    if (r >= vt->rows) r = vt->rows - 1;
    *col = c;
    *row = r;
    return my >= TAB_H;
}

static int is_word(uint32_t c) {
    return c > ' ' && !strchr(" \t\"'`()[]{}<>|;,", c < 0x80 ? (int)c : 'a');
}

/* ── input ─────────────────────────────────────────────────────────── */

static int vt_mods(void) {
    int m = banana_key_mods(), r = 0;
    if (m & BANANA_MOD_SHIFT) r |= VT_MOD_SHIFT;
    if (m & BANANA_MOD_CTRL) r |= VT_MOD_CTRL;
    if (m & BANANA_MOD_ALT) r |= VT_MOD_ALT;
    return r;
}

static void scroll_view(term_t* t, int lines) {
    tab_t* tab = t->tabs[t->cur];
    int v = tab->view + lines;
    if (v < 0) v = 0;
    if (v > tab->vt->sb_count) v = tab->vt->sb_count;
    if (v != tab->view) { tab->view = v; t->dirty = 2; }
}

static void set_zoom(int size) {
    set_font(size);g_prefs.font=g_font;
    if(term_prefs_save(&g_prefs)<0)snprintf(g_pref_status,sizeof(g_pref_status),"Could not save preferences");
    if(g_preferences.id>=0)preferences_draw();
    for (int i = 0; i < MAX_WINDOWS; i++) if (g_w[i].open) { resize_tabs(&g_w[i]); g_w[i].dirty = 2; }
}

static void key(term_t* t, int k) {
    tab_t* tab = t->tabs[t->cur];
    vt_t* vt = tab->vt;
    if (tab->dead && tab->fd < 0) { close_tab(t, t->cur); return; }
    int mods = vt_mods();
    int ctrl = mods & VT_MOD_CTRL, shift = mods & VT_MOD_SHIFT;
    /* the terminal's own keys */
    if(ctrl&&shift&&(k==','||k=='<')){preferences_open();return;}
    if (ctrl && shift && k < 0x100) {
        switch (k) {
        case 3: copy_selection(t); return;                    /* C */
        case 22: paste(t); return;                            /* V */
        case 20: add_tab(t); return;                          /* T */
        case 14: win_new(); return;                           /* N */
        case 23: close_tab(t, t->cur); return;                /* W */
        }
    }
    if (ctrl && !shift && (k == '=' || k == '+')) { set_zoom(g_font + 1); return; }
    if (ctrl && (k == '-' || k == '_') && !shift) { set_zoom(g_font - 1); return; }
    if (ctrl && k == '0') { set_zoom(15); return; }
    if (ctrl && (k == BANANA_KEY_PGUP || k == BANANA_KEY_PGDN)) {
        int n = t->ntabs;
        switch_tab(t, (t->cur + (k == BANANA_KEY_PGDN ? 1 : n - 1)) % n);
        return;
    }
    if (shift && !ctrl && !vt->alt_on) {
        if (k == BANANA_KEY_PGUP) { scroll_view(t, vt->rows - 1); return; }
        if (k == BANANA_KEY_PGDN) { scroll_view(t, -(vt->rows - 1)); return; }
        if (k == BANANA_KEY_HOME && tab->view) { scroll_view(t, vt->sb_count); return; }
        if (k == BANANA_KEY_END && tab->view) { scroll_view(t, -tab->view); return; }
    }
    char b[32];
    int n = 0;
    if (k >= 0x100) {
        n = vt_key_seq(vt, k, mods, b);
    } else {
        uint8_t c = (uint8_t)k;
        if (c == '\n') c = vt->newline_mode ? '\n' : '\r';
        if (c == '\b') c = 0x7F;                              /* Backspace: DEL, as Linux's VERASE */
        if (c == ' ' && ctrl) c = 0;
        if ((mods & VT_MOD_ALT) && c < 0x80) b[n++] = 0x1B;  /* Alt+key: ESC first (meta) */
        if (c == '\t' && shift) { memcpy(b + n, "\x1b[Z", 3); n += 3; }
        else b[n++] = (char)c;
    }
    if (n) {
        tab_send(tab, b, (size_t)n);
        if (tab->view) { tab->view = 0; t->dirty = 2; }
        if (t->has_sel && !t->selecting) { t->has_sel = 0; t->dirty = 2; }
    }
}

static void mouse_to_program(term_t* t, int button, int kind, int mx, int my) {
    tab_t* tab = t->tabs[t->cur];
    int col, row;
    cell_at(t, mx, my, &col, &row);
    char b[32];
    int n = vt_mouse_seq(tab->vt, button, kind, col, row, vt_mods(), b);
    if (n) tab_send(tab, b, (size_t)n);
}

static int program_wants_mouse(term_t* t) {
    return t->tabs[t->cur]->vt->mouse != VT_MOUSE_OFF && !(banana_key_mods() & BANANA_MOD_SHIFT);
}

static void event(term_t* t, const banana_event_t* ev) {
    tab_t* tab = t->tabs[t->cur];
    vt_t* vt = tab->vt;
    if (ev->type == BANANA_EV_MOUSE_DOWN || ev->type == BANANA_EV_MOUSE_UP || ev->type == BANANA_EV_MOUSE_MOVE) {
        t->mx = ev->x;
        t->my = ev->y;
    }
    switch (ev->type) {
    case BANANA_EV_CLOSE:
        win_close(t);
        return;
    case BANANA_EV_RESIZE:
        resize_tabs(t);
        t->dirty = 2;
        return;
    case BANANA_EV_FOCUS:
        t->focused = ev->x;
        if (vt->focus_events) tab_send(tab, ev->x ? "\x1b[I" : "\x1b[O", 3);
        vt->dirty[vt->cur.y] = 1;
        return;
    case BANANA_EV_KEY:
        key(t, ev->key);
        return;
    case BANANA_EV_WHEEL: {
        int notches = ev->y;
        if (program_wants_mouse(t)) {
            for (int i = 0; i < (notches < 0 ? -notches : notches); i++)
                mouse_to_program(t, notches < 0 ? 64 : 65, VT_MOUSE_PRESS, t->mx, t->my);
        } else if (vt->alt_on) {
            /* (full-screen programs without the mouse: the arrow keys, as xterm) */
            char b[8];
            int n = vt_key_seq(vt, notches < 0 ? VT_KEY_UP : VT_KEY_DOWN, 0, b);
            for (int i = 0; i < 3 * (notches < 0 ? -notches : notches); i++) tab_send(tab, b, (size_t)n);
        } else {
            scroll_view(t, -3 * notches);
        }
        return;
    }
    case BANANA_EV_MOUSE_DOWN: {
        if (ev->y < TAB_H) {
            if (ev->button != 1 && ev->button != 3) return;
            if (ev->x >= t->win.w - TAB_H) { add_tab(t); return; }
            if (ev->x >= t->win.w - 2*TAB_H) { preferences_open();return; }
            for (int i = 0; i < t->ntabs; i++) {
                int x0, x1;
                tab_label(t, i, &x0, &x1);
                if (ev->x < x0 || ev->x >= x1) continue;
                if (ev->button == 3 || ev->x >= x1 - 22) close_tab(t, i);       /* (the ×, or the middle button) */
                else switch_tab(t, i);
                return;
            }
            return;
        }
        if (program_wants_mouse(t)) {
            int b = ev->button == 1 ? 0 : ev->button == 3 ? 1 : 2;
            t->mouse_btn = b;
            mouse_to_program(t, b, VT_MOUSE_PRESS, ev->x, ev->y);
            return;
        }
        if (ev->button == 1) {
            int col, row;
            cell_at(t, ev->x, ev->y, &col, &row);
            spot_t s = { vt->scrolled + row - tab->view, col };
            unsigned now = banana_ticks();
            if (now - t->last_click < 400 && !spot_cmp(s, t->last_click_at)) {
                /* a double click: the word */
                const vt_line_t* L = vt_get_line(vt, (int)(s.line - vt->scrolled));
                if (L && col < L->len && is_word(L->c[col].ch)) {
                    int a = col, b = col;
                    while (a > 0 && is_word(L->c[a - 1].ch)) a--;
                    while (b + 1 < L->len && is_word(L->c[b + 1].ch)) b++;
                    t->sel_a = (spot_t){ s.line, a };
                    t->sel_b = (spot_t){ s.line, b };
                    t->has_sel = 1;
                    copy_selection(t);
                    t->dirty = 2;
                }
                t->last_click = 0;
                return;
            }
            t->last_click = now;
            t->last_click_at = s;
            t->sel_a = t->sel_b = s;
            t->selecting = 1;
            if (t->has_sel) t->dirty = 2;
            t->has_sel = 0;
        } else {
            paste(t);                                         /* middle / right button */
        }
        return;
    }
    case BANANA_EV_MOUSE_MOVE:
        if (t->mouse_btn >= 0 || (program_wants_mouse(t) && vt->mouse == VT_MOUSE_ANY)) {
            mouse_to_program(t, t->mouse_btn, VT_MOUSE_MOTION, ev->x, ev->y);
            return;
        }
        if (t->selecting && (ev->buttons & 1)) {
            int col, row;
            if (ev->y < TAB_H + PAD) scroll_view(t, 1);       /* (dragging above: back in the scrollback) */
            else if (ev->y >= t->win.h - PAD) scroll_view(t, -1);
            cell_at(t, ev->x, ev->y, &col, &row);
            spot_t s = { vt->scrolled + row - tab->view, col };
            if (spot_cmp(s, t->sel_b)) {
                t->sel_b = s;
                t->has_sel = spot_cmp(t->sel_a, t->sel_b) != 0;
                t->dirty = 2;
            }
        }
        return;
    case BANANA_EV_MOUSE_UP:
        if (t->mouse_btn >= 0) {
            mouse_to_program(t, t->mouse_btn, VT_MOUSE_RELEASE, ev->x, ev->y);
            t->mouse_btn = -1;
            return;
        }
        if (t->selecting) {
            t->selecting = 0;
            if (t->has_sel) copy_selection(t);                 /* (selected is copied, as on X11) */
        }
        return;
    }
}

/* ── the loop ──────────────────────────────────────────────────────── */

static int any_open(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) if (g_w[i].open) return 1;
    return g_preferences.id>=0;
}

/* the program of a tab ended: its tab goes */
static void reap(void) {
    int st;
    pid_t pid;
    while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            term_t* t = &g_w[i];
            if (!t->open) continue;
            for (int k = 0; k < t->ntabs; k++) if (t->tabs[k]->pid == pid) { t->tabs[k]->pid = 0; t->tabs[k]->status = st; }
        }
    }
}

int main(int argc, char** argv) {
    term_prefs_load(&g_prefs);
    if(argc==2&&!strcmp(argv[1],"--help")){
        puts("Terminal: --preferences | --show-config | --set KEY VALUE | -e COMMAND...");
        puts("Preferences: theme, font (8..40), foreground, background, cursor_color (#RRGGBB), cursor, shell");
        puts("Themes: midnight, light, solarized, mono; cursors: block, underline, bar; shells: system, bash, zsh");
        puts("Ctrl+Shift+, opens preferences. F11 toggles application fullscreen.");
        return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--show-config")){term_prefs_print(&g_prefs,stdout);return 0;}
    if(argc==4&&!strcmp(argv[1],"--set")){
        if(term_prefs_set(&g_prefs,argv[2],argv[3])<0){fprintf(stderr,"terminal: invalid preference or value\n");return 1;}
        if(g_prefs.shell==2&&access("/bin/zsh",X_OK)<0){fprintf(stderr,"terminal: Zsh is not installed\n");return 1;}
        if(term_prefs_save(&g_prefs)<0){perror("terminal preferences");return 1;}
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "-e")) g_cmd = argv + 2;
    signal(SIGPIPE, SIG_IGN);
    set_font(g_prefs.font);
    int preferences_only=argc==2&&!strcmp(argv[1],"--preferences");
    if(preferences_only)preferences_open();
    if ((preferences_only&&g_preferences.id<0)||(!preferences_only&&!win_new())) {
        printf("terminal: cannot open a window (is the desktop running? startx)\n");
        return 1;
    }
    static char buf[16384];
    struct pollfd* pf = NULL;
    tab_t** pt = NULL;
    int pcap = 0;
    while (any_open()) {
        int busy = 0;
        /* windows' events */
        for (int i = 0; i < MAX_WINDOWS; i++) {
            banana_event_t ev;
            while (g_w[i].open && bwin_event(&g_w[i].win, &ev)) { event(&g_w[i], &ev); busy = 1; }
        }
        if(g_preferences.id>=0){banana_event_t ev;while(g_preferences.id>=0&&bwin_event(&g_preferences,&ev)){preferences_event(&ev);busy=1;}}
        /* the programs' output */
        int n = 0;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            term_t* t = &g_w[i];
            if (!t->open) continue;
            for (int k = 0; k < t->ntabs; k++) {
                if (t->tabs[k]->fd < 0) continue;
                if (n == pcap) {
                    pcap = pcap ? pcap * 2 : 16;
                    pf = realloc(pf, sizeof(*pf) * (size_t)pcap);
                    pt = realloc(pt, sizeof(*pt) * (size_t)pcap);
                    if (!pf || !pt) return 1;
                }
                tab_flush(t->tabs[k]);
                pf[n].fd = t->tabs[k]->fd;
                pf[n].events = POLLIN;
                pf[n].revents = 0;
                pt[n++] = t->tabs[k];
            }
        }
        if (n && poll(pf, (nfds_t)n, busy ? 0 : 15) > 0) {
            for (int j = 0; j < n; j++) {
                if (!pf[j].revents) continue;
                tab_t* tab = pt[j];
                /* (a lot at once: some, then the windows get their turn) */
                for (int round = 0; round < 16; round++) {
                    ssize_t r = read(tab->fd, buf, sizeof(buf));
                    if (r > 0) {
                        vt_write(tab->vt, buf, (size_t)r);
                        continue;
                    }
                    if (r < 0 && errno == EAGAIN) break;
                    /* the end (EIO: every program on it closed it) */
                    close(tab->fd);
                    tab->fd = -1;
                    break;
                }
                if (tab->vt->reply_len) {
                    tab_send(tab, tab->vt->reply, (size_t)tab->vt->reply_len);
                    tab->vt->reply_len = 0;
                }
                if (tab->vt->title_changed) {
                    tab->vt->title_changed = 0;
                    snprintf(tab->title, sizeof(tab->title), "%s", tab->vt->title[0] ? tab->vt->title : "Terminal");
                }
            }
        } else if (!n) {
            banana_sleep(15);
        }
        reap();
        /* tabs whose program is gone close (or say why); windows redraw what changed */
        unsigned now = banana_ticks();
        for (int i = 0; i < MAX_WINDOWS; i++) {
            term_t* t = &g_w[i];
            for (int k = t->open ? t->ntabs - 1 : -1; k >= 0 && t->open; k--) {
                tab_t* tab = t->tabs[k];
                tab->vt->bell = 0;
                if (tab->fd >= 0 || tab->pid || tab->dead) continue;
                int st = tab->status;
                if (WIFEXITED(st) && WEXITSTATUS(st) == 0) { close_tab(t, k); continue; }
                if (WIFSIGNALED(st) && WTERMSIG(st) == SIGHUP) { close_tab(t, k); continue; }
                char m[96];
                if (WIFSIGNALED(st)) snprintf(m, sizeof(m), "\r\n\x1b[0;7m[ended by signal %d - a key closes the tab]\x1b[0m", WTERMSIG(st));
                else snprintf(m, sizeof(m), "\r\n\x1b[0;7m[exited with status %d - a key closes the tab]\x1b[0m", WEXITSTATUS(st));
                vt_write(tab->vt, m, strlen(m));
                tab->dead = 1;
            }
            if (!t->open) continue;
            vt_t* vt = t->tabs[t->cur]->vt;
            int changed = t->dirty || t->bar_dirty || vt->all_dirty;
            for (int r = 0; !changed && r < vt->rows; r++) changed = vt->dirty[r];
            if (changed && now - t->last_draw >= 16) {
                draw(t);
                t->last_draw = now;
            }
        }
    }
    return 0;
}
