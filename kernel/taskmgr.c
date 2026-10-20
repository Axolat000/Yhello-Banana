/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "taskmgr.h"
#include "gfx.h"
#include "kstring.h"
#include "kheap.h"
#include "meminfo.h"
#include "zswap.h"
#include "timer.h"
#include "task.h"
#include "smp.h"
#include "fs.h"
#include "winframe.h"
#include "ctxmenu.h"
#include "app.h"
#include "gui.h"
#include "audio.h"
#include "sensors.h"
#include "sysinfo.h"
#include "disk.h"
#include "blockdev.h"
#include "gpu.h"
#include "fb.h"
#include "kbnav.h"
#include "../net/net.h"

/*
 * Laid out like Windows 11's: a rail of pages at the left - Processes,
 * Performance, Details, Users - and the page beside it. Processes: the
 * apps (windows and running programs) and the background processes (the
 * kernel's tasks) with their CPU, memory, disk and network use, the
 * columns tinted by how much. Performance: a tile per device (CPU,
 * memory, disk, Ethernet, GPU) with its live graph; the one chosen in big
 * with its numbers. Sampled once a second while the window is open.
 */

#define TITLE_H  20
#define RAIL_W   150
#define ITEM_H   34
#define ROW_H    20
#define HIST     60
#define MAX_ROWS 96
#define MAX_CORE 16

/* Windows 11's dark look */
#define C_BG     0x00202020u
#define C_RAIL   0x001A1A1Au
#define C_PANEL  0x00272727u
#define C_LINE   0x00383838u
#define C_HOVER  0x002D2D2Du
#define C_TEXT   0x00F2F2F2u
#define C_DIM    0x00A8A8A8u
#define C_ACCENT 0x0060CDFFu
#define C_SEL    0x00334E66u
#define C_CPU    0x0017A2E8u
#define C_MEM    0x00A066D0u
#define C_DISK   0x004DB858u
#define C_NET    0x00D88A3Cu
#define C_GPU    0x00D0506Au

enum { P_PROCS = 0, P_PERF, P_DETAILS, P_USERS, P_COUNT };
static const char* const PAGE_NAMES[P_COUNT] = { "Processes", "Performance", "Details", "Users" };
enum { D_CPU = 0, D_MEM, D_DISK, D_NET, D_GPU, D_COUNT };

/* a row of the Processes page: a window, a running app, or a kernel task */
enum { R_WINDOW, R_APP, R_TASK, R_GROUP };
typedef struct {
    int kind, handle, pid;
    char text[48];
    char status[20];
    int cpu;                  /* % (-1: not known) */
    uint32_t mem;             /* bytes (0: not known) */
} row_t;

static int        g_open;
static win_geom_t g_win = { .x = 20, .y = 20, .w = 760, .h = 520, .min_w = 560, .min_h = 380 };
static int        g_page, g_dev;
static int        g_sel = -1, g_scroll;
static int        g_cores_view;          /* Performance > CPU: a graph per logical processor */
static uint32_t   g_gen, g_sample_ms;
static char       g_status[96];
static kbnav_t    g_nav;

/* the history, one sample a second */
static uint8_t  g_cpu_hist[HIST], g_mem_hist[HIST], g_disk_hist[HIST];
static uint32_t g_net_hist[HIST];                     /* bytes a second, in + out */
static uint8_t  g_core_hist[MAX_CORE][HIST];
static int      g_hist_n;
static uint32_t g_rd_rate, g_wr_rate, g_rx_rate, g_tx_rate;   /* bytes a second, the last second */
static uint64_t g_last_rd, g_last_wr, g_last_busy;
static uint32_t g_last_rx, g_last_tx, g_last_ms;

static row_t    g_rows[MAX_ROWS];
static int      g_nrows, g_napps, g_nbg;
static int      g_menu_row = -1, g_menu_pid = -1;

static task_info_t g_tasks[TASK_MAX];
static int         g_ntasks;

/* ── drawing helpers ── */
static void frame(int x, int y, int w, int h, uint32_t bg, uint32_t line) {
    gfx_fill_rect(x, y, w, h, bg);
    gfx_fill_rect(x, y, w, 1, line);
    gfx_fill_rect(x, y + h - 1, w, 1, line);
    gfx_fill_rect(x, y, 1, h, line);
    gfx_fill_rect(x + w - 1, y, 1, h, line);
}

static uint32_t mix(uint32_t a, uint32_t b, int t) {        /* t: 0..256 from a to b */
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    uint32_t r = (((a >> 16) & 255) * (uint32_t)(256 - t) + ((b >> 16) & 255) * (uint32_t)t) >> 8;
    uint32_t g = (((a >> 8) & 255) * (uint32_t)(256 - t) + ((b >> 8) & 255) * (uint32_t)t) >> 8;
    uint32_t c = ((a & 255) * (uint32_t)(256 - t) + (b & 255) * (uint32_t)t) >> 8;
    return r << 16 | g << 8 | c;
}

/* the heat of a column's cell: darker to amber as it gets busier */
static uint32_t heat(int pct) {
    if (pct <= 0) return 0x002A2716u;
    return mix(0x00332E19u, 0x00866A12u, pct * 256 / 100);
}

static void button(int x, int y, int w, const char* label, int enabled) {
    kbnav_add(&g_nav, x, y, w, 24);
    frame(x, y, w, 24, enabled ? 0x002D2D2Du : 0x00252525u, enabled ? 0x00464646u : 0x00303030u);
    gfx_draw_text(x + (w - (int)strlen(label) * 8) / 2, y + 8, label, enabled ? C_TEXT : 0x00686868u, enabled ? 0x002D2D2Du : 0x00252525u);
}

static int inside(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

/* (KiB: more than 4 GiB fits) */
static void human_kb(uint32_t kb, char* out, int cap) {
    if (kb >= (1u << 20)) ksnprintf(out, (size_t)cap, "%u.%u GB", kb >> 20, ((kb >> 10) & 1023) * 10 / 1024);
    else if (kb >= 1024) ksnprintf(out, (size_t)cap, "%u.%u MB", kb >> 10, (kb & 1023) * 10 / 1024);
    else ksnprintf(out, (size_t)cap, "%u KB", kb);
}

static void human_bytes64(uint64_t b, char* out, int cap) {
    if (b >= (1ull << 30)) ksnprintf(out, (size_t)cap, "%u.%u GB", (uint32_t)(b >> 30), (uint32_t)((b >> 20) & 1023) * 10 / 1024);
    else if (b >= (1ull << 20)) ksnprintf(out, (size_t)cap, "%u.%u MB", (uint32_t)(b >> 20), (uint32_t)((b >> 10) & 1023) * 10 / 1024);
    else ksnprintf(out, (size_t)cap, "%u KB", (uint32_t)(b >> 10));
}

/* a rate: "1.5 MB/s" */
static void rate(uint32_t bps, char* out, int cap) {
    if (bps >= (1u << 20)) ksnprintf(out, (size_t)cap, "%u.%u MB/s", bps >> 20, ((bps >> 10) & 1023) * 10 / 1024);
    else ksnprintf(out, (size_t)cap, "%u KB/s", bps >> 10);
}

/* network: "120 Kbps" (bits, as Windows) */
static void bits(uint32_t bps, char* out, int cap) {
    uint32_t kb = bps / 125;                          /* bytes/s -> kilobits/s */
    if (kb >= 10000) ksnprintf(out, (size_t)cap, "%u Mbps", kb / 1000);
    else if (kb >= 1000) ksnprintf(out, (size_t)cap, "%u.%u Mbps", kb / 1000, kb % 1000 / 100);
    else ksnprintf(out, (size_t)cap, "%u Kbps", kb);
}

/* ── the numbers ── */
static void snap_tasks(void) {
    g_ntasks = task_count();
    if (g_ntasks > TASK_MAX) g_ntasks = TASK_MAX;
    task_snapshot(g_tasks, g_ntasks);
}

static uint32_t cpu_now(void) {
    int cores = cpu_count() < 1 ? 1 : cpu_count();
    uint32_t sum = 0;
    for (int c = 0; c < cores && c < MAX_CORE; c++) sum += task_core_pct(c);
    return sum / (uint32_t)(cores < MAX_CORE ? cores : MAX_CORE);
}

static uint32_t mem_pct(void) {
    meminfo_t m;
    meminfo_get(&m);
    /* (saved file data kept in RAM is a cache, given back when needed: not "in use") */
    return m.total_kb ? (uint32_t)((uint64_t)(m.used_kb - m.cached_kb) * 100 / m.total_kb) : 0;
}

static netdev_t* net_dev(void) { netif_t* n = net_if(); return n ? n->dev : NULL; }

static void push(uint8_t* h, uint32_t v) {
    if (g_hist_n == HIST) memmove(h, h + 1, HIST - 1);
    h[g_hist_n == HIST ? HIST - 1 : g_hist_n] = (uint8_t)(v > 100 ? 100 : v);
}

static void sample(void) {
    uint32_t now = timer_ms(), dt = g_last_ms ? now - g_last_ms : 0;
    uint64_t rd, wr, busy;
    disk_io_stats(&rd, &wr, &busy);
    netdev_t* nd = net_dev();
    uint32_t rx = nd ? nd->rx_bytes : 0, tx = nd ? nd->tx_bytes : 0;
    uint32_t disk = 0;
    if (dt) {
        g_rd_rate = (uint32_t)((rd - g_last_rd) * 1000u / dt);
        g_wr_rate = (uint32_t)((wr - g_last_wr) * 1000u / dt);
        uint64_t b = (busy - g_last_busy) / 10000u;          /* (ns -> hundredths of a ms: % of dt ms) */
        disk = (uint32_t)(b / dt);
        g_rx_rate = (uint32_t)((uint64_t)(rx - g_last_rx) * 1000u / dt);
        g_tx_rate = (uint32_t)((uint64_t)(tx - g_last_tx) * 1000u / dt);
    }
    g_last_rd = rd; g_last_wr = wr; g_last_busy = busy; g_last_rx = rx; g_last_tx = tx; g_last_ms = now;

    push(g_cpu_hist, cpu_now());
    push(g_mem_hist, mem_pct());
    push(g_disk_hist, disk);
    for (int c = 0; c < MAX_CORE; c++) push(g_core_hist[c], c < cpu_count() ? task_core_pct(c) : 0);
    if (g_hist_n == HIST) memmove(g_net_hist, g_net_hist + 1, (HIST - 1) * sizeof(uint32_t));
    g_net_hist[g_hist_n == HIST ? HIST - 1 : g_hist_n] = g_rx_rate + g_tx_rate;
    if (g_hist_n < HIST) g_hist_n++;
}

static int last(const uint8_t* h) { return g_hist_n ? h[g_hist_n - 1] : 0; }

/* the Processes page's rows: apps (windows, then programs), then the kernel's tasks */
static int task_cpu_of(int pid) {
    for (int i = 0; i < g_ntasks; i++) if (g_tasks[i].state != TASK_UNUSED && (int)g_tasks[i].pid == pid) return (int)g_tasks[i].cpu_pct;
    return -1;
}

static void build_rows(void) {
    snap_tasks();
    static gui_win_info_t w[64];
    int nw = gui_windows(w, 64);
    static app_info_t a[APP_MAX];
    int na = app_snapshot(a, APP_MAX);
    g_nrows = 0;
    g_napps = nw + na;
    row_t* r = &g_rows[g_nrows++];
    memset(r, 0, sizeof(*r));
    r->kind = R_GROUP;
    ksnprintf(r->text, sizeof(r->text), "Apps (%d)", g_napps);
    for (int i = 0; i < nw && g_nrows < MAX_ROWS; i++) {
        r = &g_rows[g_nrows++];
        memset(r, 0, sizeof(*r));
        r->kind = R_WINDOW;
        r->handle = w[i].handle;
        r->cpu = -1;
        kstrlcpy(r->text, w[i].title, sizeof(r->text));
        kstrlcpy(r->status, w[i].minimized ? "Minimized" : w[i].focused ? "Active" : "", sizeof(r->status));
    }
    for (int i = 0; i < na && g_nrows < MAX_ROWS; i++) {
        r = &g_rows[g_nrows++];
        memset(r, 0, sizeof(*r));
        r->kind = R_APP;
        r->handle = a[i].id;
        r->pid = a[i].pid;
        r->cpu = task_cpu_of(a[i].pid);
        r->mem = a[i].mem;
        ksnprintf(r->text, sizeof(r->text), "%s", a[i].name);
        ksnprintf(r->status, sizeof(r->status), "%d thread%s", a[i].threads, a[i].threads == 1 ? "" : "s");
    }
    int gi = g_nrows;
    if (g_nrows < MAX_ROWS) { r = &g_rows[g_nrows++]; memset(r, 0, sizeof(*r)); r->kind = R_GROUP; }
    g_nbg = 0;
    for (int i = 0; i < g_ntasks && g_nrows < MAX_ROWS; i++) {
        if (g_tasks[i].state == TASK_UNUSED) continue;
        r = &g_rows[g_nrows++];
        memset(r, 0, sizeof(*r));
        r->kind = R_TASK;
        r->pid = (int)g_tasks[i].pid;
        r->cpu = (int)g_tasks[i].cpu_pct;
        kstrlcpy(r->text, g_tasks[i].name, sizeof(r->text));
        kstrlcpy(r->status, task_state_str(g_tasks[i].state), sizeof(r->status));
        g_nbg++;
    }
    if (gi < g_nrows) ksnprintf(g_rows[gi].text, sizeof(g_rows[gi].text), "Background processes (%d)", g_nbg);
    if (g_sel >= g_nrows) g_sel = -1;
}

static void end_task(int row) {
    if (row < 0 || row >= g_nrows) return;
    row_t* r = &g_rows[row];
    if (r->kind == R_APP) app_kill(r->handle);
    else if (r->kind == R_WINDOW) gui_window_close(r->handle);
    else return;
    ksnprintf(g_status, sizeof(g_status), "Ended %s", r->text);
    g_gen++;
}

static void switch_to(int row) {
    if (row < 0 || row >= g_nrows || g_rows[row].kind != R_WINDOW) return;
    gui_window_activate(g_rows[row].handle);
}

/* the processor's caches (cpuid leaf 4, Intel; 0x80000005/6, AMD): KiB, 0 unknown */
static void cpuid(uint32_t leaf, uint32_t sub, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}

static void caches(uint32_t* l1, uint32_t* l2, uint32_t* l3) {
    uint32_t a, b, c, d;
    *l1 = *l2 = *l3 = 0;
    cpuid(0, 0, &a, &b, &c, &d);
    uint32_t maxl = a;
    if (b == 0x756E6547 && maxl >= 4) {                 /* "Genu"ineIntel */
        for (uint32_t i = 0; i < 8; i++) {
            cpuid(4, i, &a, &b, &c, &d);
            uint32_t type = a & 31, level = (a >> 5) & 7;
            if (!type) break;
            uint32_t size = ((b >> 22) + 1) * (((b >> 12) & 1023) + 1) * ((b & 4095) + 1) * (c + 1) / 1024;
            if (level == 1) *l1 += size;
            else if (level == 2) *l2 = size;
            else if (level == 3) *l3 = size;
        }
        return;
    }
    cpuid(0x80000000u, 0, &a, &b, &c, &d);
    if (a >= 0x80000006u) {
        cpuid(0x80000005u, 0, &a, &b, &c, &d);
        *l1 = (c >> 24) + (d >> 24);
        cpuid(0x80000006u, 0, &a, &b, &c, &d);
        *l2 = c >> 16;
        *l3 = (d >> 18) * 512;
    }
}

static int in_vm(void) {
    uint32_t a, b, c, d;
    cpuid(1, 0, &a, &b, &c, &d);
    return (c >> 31) & 1;
}

/* ── public ───────────────────────────────────────────────────────── */

void taskmgr_open(void) {
    g_open = 1;
    g_status[0] = 0;
    if (!g_hist_n) sample();
    build_rows();
    win_clamp(&g_win);
    g_gen++;
}

void taskmgr_close(void) {
    g_open = 0;
    g_win.dragging = g_win.resizing = 0;
    g_gen++;
}

int taskmgr_is_open(void) { return g_open; }
int taskmgr_contains(int mx, int my) { return g_open && inside(mx, my, g_win.x, g_win.y, g_win.w, g_win.h); }

uint32_t taskmgr_signature(void) {
    if (!g_open) return 0;
    if (timer_ms() - g_sample_ms >= 1000) {        /* live: once a second */
        g_sample_ms = timer_ms();
        sample();
        build_rows();
        g_gen++;
    }
    return g_gen * 2654435761u ^ (uint32_t)(g_win.x << 16 | g_win.y) ^ (uint32_t)(g_win.w << 20 | g_win.h << 4);
}

/* ── layout ── */
static int content_x(void) { return g_win.x + RAIL_W + 14; }
static int content_w(void) { return g_win.w - RAIL_W - 22; }
static int head_y(void) { return g_win.y + TITLE_H + 12; }
static int table_y(void) { return head_y() + 40; }               /* the column headers */
static int rows_y(void) { return table_y() + 34; }
static int rows_max(void) { int n = (g_win.y + g_win.h - 10 - rows_y()) / ROW_H; return n < 1 ? 1 : n; }

/* the Processes / Details columns, from the right: x of each */
#define COL_NET  86
#define COL_DISK 86
#define COL_MEM  96
#define COL_CPU  66
#define COL_STAT 100
static int col_x(int i) {        /* 0 status, 1 cpu, 2 mem, 3 disk, 4 net */
    int r = content_x() + content_w();
    int w[5] = { COL_STAT, COL_CPU, COL_MEM, COL_DISK, COL_NET };
    for (int k = 4; k >= i; k--) r -= w[k];
    return r;
}

static int row_at(int mx, int my) {
    if (mx < content_x() || mx >= content_x() + content_w() || my < rows_y()) return -1;
    int r = (my - rows_y()) / ROW_H;
    if (r >= rows_max()) return -1;
    r += g_scroll;
    return r < g_nrows ? r : -1;
}

/* Details: the task on list row r */
static int detail_pid_at(int mx, int my) {
    if (mx < content_x() || my < rows_y()) return -1;
    int r = (my - rows_y()) / ROW_H + g_scroll;
    for (int i = 0; i < g_ntasks; i++) {
        if (g_tasks[i].state == TASK_UNUSED) continue;
        if (r-- == 0) return (int)g_tasks[i].pid;
    }
    return -1;
}

static const struct { const char* name; int nice; } PRIOS[] = {
    { "High", -10 }, { "Above normal", -5 }, { "Normal", 0 }, { "Below normal", 5 }, { "Low", 10 },
};

static void menu_cb(int id, void* arg) {
    (void)arg;
    if (id == 1) switch_to(g_menu_row);
    else if (id == 2) end_task(g_menu_row);
    else if (id == 3) { g_page = P_DETAILS; g_scroll = 0; }
    else if (id >= 10 && id < 15 && g_menu_pid >= 0) {
        int k = id - 10;
        if (task_set_nice(g_menu_pid, PRIOS[k].nice) == 0)
            ksnprintf(g_status, sizeof(g_status), "Task %d: priority %s (nice %d)", g_menu_pid, PRIOS[k].name, PRIOS[k].nice);
    }
    g_gen++;
}

static void prio_menu(int mx, int my, int pid) {
    g_menu_pid = pid;
    int cur = task_get_nice(pid);
    static char labels[5][32];
    ctx_item_t items[5];
    for (int k = 0; k < 5; k++) {
        ksnprintf(labels[k], sizeof(labels[k]), "%s%s", cur == PRIOS[k].nice ? "* " : "  ", PRIOS[k].name);
        items[k].label = labels[k];
        items[k].id = 10 + k;
        items[k].disabled = 0;
    }
    ctxmenu_open(mx, my, items, 5, menu_cb, NULL);
}

void taskmgr_rclick(int mx, int my) {
    g_gen++;
    if (g_page == P_DETAILS) {
        int pid = detail_pid_at(mx, my);
        if (pid >= 0) prio_menu(mx, my, pid);
        return;
    }
    if (g_page == P_PERF) {
        if (g_dev == D_CPU) g_cores_view = !g_cores_view;     /* (Windows: "Change graph to") */
        return;
    }
    if (g_page != P_PROCS) return;
    int r = row_at(mx, my);
    if (r < 0 || g_rows[r].kind == R_GROUP) return;
    g_sel = r;
    g_menu_row = r;
    if (g_rows[r].kind == R_TASK) { prio_menu(mx, my, g_rows[r].pid); return; }
    ctx_item_t items[] = {
        { "Switch to", 1, g_rows[r].kind != R_WINDOW },
        { "End task", 2, 0 },
        { CTX_SEP, 0, 0 },
        { "Go to details", 3, 0 },
    };
    ctxmenu_open(mx, my, items, 4, menu_cb, NULL);
}

void taskmgr_click(int mx, int my) {
    if (!taskmgr_contains(mx, my)) return;
    int ly = my - g_win.y;
    g_gen++;
    if (ly < TITLE_H + 2) {
        int b = win_button_press(&g_win, 4, 12, mx, my);
        if (b == WIN_BTN_CLOSE) { taskmgr_close(); return; }
        if (b) return;
        win_title_press(&g_win, mx, my);
        return;
    }
    if (win_grip_press(&g_win, mx, my)) return;
    kbnav_mouse(&g_nav);
    /* the rail */
    if (mx < g_win.x + RAIL_W) {
        int i = (my - (g_win.y + TITLE_H + 10)) / ITEM_H;
        if (my >= g_win.y + TITLE_H + 10 && i >= 0 && i < P_COUNT) { g_page = i; g_sel = -1; g_scroll = 0; g_status[0] = 0; }
        return;
    }
    int cx = content_x(), cw = content_w(), hy = head_y();
    if (g_page == P_PROCS) {
        if (my >= hy && my < hy + 24) {
            if (mx >= cx + cw - 96 && mx < cx + cw) end_task(g_sel);
            else if (mx >= cx + cw - 196 && mx < cx + cw - 104) switch_to(g_sel);
            return;
        }
        int r = row_at(mx, my);
        static uint32_t last_ms;
        static int last_row = -1;
        if (r >= 0 && g_rows[r].kind != R_GROUP) {
            uint32_t now = timer_ms();
            if (r == last_row && now - last_ms < 450) switch_to(r);   /* double-click */
            last_row = r;
            last_ms = now;
            g_sel = r;
        } else g_sel = -1;
    } else if (g_page == P_PERF) {
        int ty = hy + 40;
        if (mx < cx + 200) {
            int d = (my - ty) / 62;
            if (my >= ty && d >= 0 && d < D_COUNT) g_dev = d;
        } else if (g_dev == D_CPU && cpu_count() > 1 && my >= ty + 20 && my < ty + 220) {
            g_cores_view = !g_cores_view;
        }
    }
}

int taskmgr_navkey(int code) {
    if (!g_open) return 0;
    g_gen++;
    if (code == KB_DEL && g_page == P_PROCS && g_sel >= 0) { end_task(g_sel); return 1; }
    if (g_page == P_PROCS && (code == KB_UP || code == KB_DOWN) && g_nrows) {
        int s = g_sel < 0 ? 0 : g_sel;
        do { s += code == KB_UP ? -1 : 1; } while (s >= 0 && s < g_nrows && g_rows[s].kind == R_GROUP);
        if (s >= 0 && s < g_nrows) g_sel = s;
        if (g_sel < g_scroll) g_scroll = g_sel;
        if (g_sel >= g_scroll + rows_max()) g_scroll = g_sel - rows_max() + 1;
        return 1;
    }
    if (g_page == P_PROCS && code == KB_ENTER && g_sel >= 0) { switch_to(g_sel); return 1; }
    if (g_page == P_PERF && (code == KB_UP || code == KB_DOWN)) {
        g_dev = (g_dev + (code == KB_UP ? D_COUNT - 1 : 1)) % D_COUNT;
        return 1;
    }
    int cx, cy;
    int r = kbnav_key(&g_nav, code, &cx, &cy);
    if (r == 1) {
        int shown = g_nav.shown, focus = g_nav.focus;
        taskmgr_click(cx, cy);
        g_nav.shown = shown;
        g_nav.focus = focus;
    }
    return r != 0;
}

void taskmgr_mouse(int mx, int my, int left) {
    if (win_mouse(&g_win, mx, my, left)) g_gen++;
}

void taskmgr_wheel(int mx, int my, int dz) {
    (void)mx; (void)my;
    int total = g_page == P_DETAILS ? g_ntasks : g_nrows;
    g_scroll += dz * 3;
    if (g_scroll > total - rows_max()) g_scroll = total - rows_max();
    if (g_scroll < 0) g_scroll = 0;
    g_gen++;
}

/* ── the pages ── */

/* the rail's icons: small line drawings */
static void rail_icon(int page, int x, int y, uint32_t c) {
    switch (page) {
    case P_PROCS:            /* a grid of four boxes */
        for (int i = 0; i < 4; i++) frame(x + (i & 1) * 8, y + (i >> 1) * 8, 7, 7, C_RAIL, c);
        break;
    case P_PERF:             /* a pulse line */
        gfx_fill_rect(x, y + 8, 4, 1, c); gfx_fill_rect(x + 4, y + 3, 1, 6, c); gfx_fill_rect(x + 5, y + 3, 1, 1, c);
        gfx_fill_rect(x + 6, y + 3, 1, 10, c); gfx_fill_rect(x + 7, y + 12, 1, 1, c); gfx_fill_rect(x + 8, y + 6, 1, 7, c);
        gfx_fill_rect(x + 9, y + 6, 1, 1, c); gfx_fill_rect(x + 10, y + 6, 1, 3, c); gfx_fill_rect(x + 11, y + 8, 4, 1, c);
        break;
    case P_DETAILS:          /* a bulleted list */
        for (int i = 0; i < 3; i++) { gfx_fill_rect(x, y + 2 + i * 5, 2, 2, c); gfx_fill_rect(x + 4, y + 2 + i * 5, 10, 1, c); }
        break;
    default:                 /* a person */
        frame(x + 4, y, 7, 7, C_RAIL, c);
        frame(x + 1, y + 8, 13, 7, C_RAIL, c);
        break;
    }
}

static void draw_rail(void) {
    int x = g_win.x + 3, y = g_win.y + TITLE_H + 2, h = g_win.h - TITLE_H - 5;
    gfx_fill_rect(x, y, RAIL_W - 3, h, C_RAIL);
    for (int i = 0; i < P_COUNT; i++) {
        int iy = g_win.y + TITLE_H + 10 + i * ITEM_H, on = i == g_page;
        kbnav_add(&g_nav, x + 4, iy, RAIL_W - 12, ITEM_H - 4);
        if (on) {
            gfx_fill_rect(x + 4, iy, RAIL_W - 12, ITEM_H - 4, C_HOVER);
            gfx_fill_rect(x + 4, iy + 8, 3, ITEM_H - 20, C_ACCENT);
        }
        uint32_t bg = on ? C_HOVER : C_RAIL;
        rail_icon(i, x + 16, iy + 7, on ? C_ACCENT : C_DIM);
        (void)bg;
        gfx_draw_text(x + 42, iy + 11, PAGE_NAMES[i], on ? C_TEXT : C_DIM, bg);
    }
}

static void page_title(const char* t) {
    gfx_draw_text_scaled(content_x(), head_y() + 4, 2, t, C_TEXT, C_BG);
}

/* a column header: the total over its name (Windows: "12%" over "CPU") */
static void col_head(int x, int w, const char* total, const char* name, int pct) {
    uint32_t bg = pct >= 0 ? heat(pct / 2) : C_BG;
    gfx_fill_rect(x, table_y(), w - 2, 32, bg);
    if (total) gfx_draw_text(x + w - 10 - (int)strlen(total) * 8, table_y() + 5, total, C_TEXT, bg);
    gfx_draw_text(x + w - 10 - (int)strlen(name) * 8, table_y() + 19, name, C_DIM, bg);
}

static void draw_processes(void) {
    int cx = content_x(), cw = content_w();
    page_title("Processes");
    int sel_ok = g_sel >= 0 && g_sel < g_nrows;
    button(cx + cw - 96, head_y(), 96, "End task", sel_ok && g_rows[g_sel].kind != R_TASK);
    button(cx + cw - 196, head_y(), 92, "Switch to", sel_ok && g_rows[g_sel].kind == R_WINDOW);

    char t[24];
    int cpu = last(g_cpu_hist), mem = last(g_mem_hist), dsk = last(g_disk_hist);
    gfx_draw_text(cx + 8, table_y() + 19, "Name", C_DIM, C_BG);
    col_head(col_x(0), COL_STAT, NULL, "Status", -1);
    ksnprintf(t, sizeof(t), "%d%%", cpu);       col_head(col_x(1), COL_CPU, t, "CPU", cpu);
    ksnprintf(t, sizeof(t), "%d%%", mem);       col_head(col_x(2), COL_MEM, t, "Memory", mem);
    ksnprintf(t, sizeof(t), "%d%%", dsk);       col_head(col_x(3), COL_DISK, t, "Disk", dsk);
    netdev_t* nd = net_dev();
    int netp = nd ? (int)((g_rx_rate + g_tx_rate) / 125000u) : 0;   /* % of a 100 Mbps link (approximate) */
    ksnprintf(t, sizeof(t), "%d%%", netp > 100 ? 100 : netp);
    col_head(col_x(4), COL_NET, t, "Network", netp);
    gfx_fill_rect(cx, rows_y() - 2, cw, 1, C_LINE);

    int maxr = rows_max();
    if (g_scroll > g_nrows - maxr) g_scroll = g_nrows - maxr;
    if (g_scroll < 0) g_scroll = 0;
    for (int i = 0; i < maxr && i + g_scroll < g_nrows; i++) {
        int ri = i + g_scroll, ry = rows_y() + i * ROW_H;
        row_t* r = &g_rows[ri];
        if (r->kind == R_GROUP) {
            gfx_draw_text(cx + 8, ry + 8, r->text, C_ACCENT, C_BG);
            continue;
        }
        int sel = ri == g_sel;
        uint32_t bg = sel ? C_SEL : C_BG;
        kbnav_add(&g_nav, cx, ry, cw, ROW_H);
        if (sel) gfx_fill_rect(cx, ry, cw, ROW_H, bg);
        /* its icon: blue a window, amber an app, grey a kernel task */
        uint32_t ic = r->kind == R_WINDOW ? 0x003A7BD5u : r->kind == R_APP ? 0x00D0A030u : 0x00707A88u;
        frame(cx + 10, ry + 5, 10, 10, ic, mix(ic, 0x00FFFFFFu, 80));
        char nm[48];
        kstrlcpy(nm, r->text, sizeof(nm));
        int maxc = (col_x(0) - (cx + 30) - 6) / 8;
        if (maxc > 0 && maxc < (int)sizeof(nm) && (int)strlen(nm) > maxc) { nm[maxc] = 0; if (maxc > 1) nm[maxc - 1] = '.'; }
        gfx_draw_text(cx + 30, ry + 6, nm, C_TEXT, bg);
        gfx_draw_text(col_x(0) + 8, ry + 6, r->status, C_DIM, bg);
        /* the busy columns: tinted by how much */
        uint32_t hb;
        hb = sel ? bg : heat(r->cpu < 0 ? 0 : r->cpu);
        gfx_fill_rect(col_x(1), ry, COL_CPU - 2, ROW_H - 1, hb);
        if (r->cpu >= 0) { ksnprintf(t, sizeof(t), "%d%%", r->cpu); gfx_draw_text(col_x(1) + COL_CPU - 10 - (int)strlen(t) * 8, ry + 6, t, C_TEXT, hb); }
        int memp = r->mem ? (int)(r->mem / (1u << 20)) : 0;
        hb = sel ? bg : heat(memp > 100 ? 100 : memp);
        gfx_fill_rect(col_x(2), ry, COL_MEM - 2, ROW_H - 1, hb);
        if (r->mem) { human_kb(r->mem / 1024, t, sizeof(t)); gfx_draw_text(col_x(2) + COL_MEM - 10 - (int)strlen(t) * 8, ry + 6, t, C_TEXT, hb); }
        hb = sel ? bg : heat(0);
        gfx_fill_rect(col_x(3), ry, COL_DISK - 2, ROW_H - 1, hb);
        gfx_fill_rect(col_x(4), ry, COL_NET - 2, ROW_H - 1, hb);
        /* (the kernel does every disk and network transfer: shown on its first task) */
        if (r->kind == R_TASK && r->pid == 0) {
            rate(g_rd_rate + g_wr_rate, t, sizeof(t));
            gfx_draw_text(col_x(3) + COL_DISK - 10 - (int)strlen(t) * 8, ry + 6, t, C_TEXT, hb);
            bits(g_rx_rate + g_tx_rate, t, sizeof(t));
            gfx_draw_text(col_x(4) + COL_NET - 10 - (int)strlen(t) * 8, ry + 6, t, C_TEXT, hb);
        }
    }
}

/* a graph of a history: values 0..max, the newest at the right, filled under its line */
static void big_graph(int x, int y, int w, int h, const uint8_t* h8, const uint32_t* h32, uint32_t max, uint32_t color) {
    uint32_t fill = mix(C_PANEL, color, 70), grid = mix(C_PANEL, color, 40);
    frame(x, y, w, h, C_PANEL, color);
    for (int g = 1; g < 10; g++) gfx_fill_rect(x + 1, y + h * g / 10, w - 2, 1, grid);
    for (int g = 1; g < 6; g++) gfx_fill_rect(x + w * g / 6, y + 1, 1, h - 2, grid);
    if (!g_hist_n || !max) return;
    int prev = -1;
    for (int px = 1; px < w - 1; px++) {
        /* where px falls in the last HIST samples (the history fills from the right) */
        int pos = (px - 1) * (HIST - 1) * 16 / (w - 3);          /* 1/16ths of a sample */
        int i = pos / 16, fr = pos % 16, start = HIST - g_hist_n;
        if (i < start) continue;
        int k = i - start;
        uint32_t v0 = h8 ? h8[k] : h32[k];
        uint32_t v1 = k + 1 < g_hist_n ? (h8 ? h8[k + 1] : h32[k + 1]) : v0;
        uint32_t v = (v0 * (uint32_t)(16 - fr) + v1 * (uint32_t)fr) / 16;
        if (v > max) v = max;
        int vh = (int)((uint64_t)v * (uint32_t)(h - 2) / max);
        int top = y + h - 1 - vh;
        if (vh > 0) gfx_fill_rect(x + px, top, 1, vh, fill);
        if (prev >= 0) {
            int a = prev < top ? prev : top, b = prev < top ? top : prev;
            gfx_fill_rect(x + px, a, 1, b - a + 1, color);
        } else gfx_fill_rect(x + px, top, 1, 1, color);
        prev = top;
    }
}

static uint32_t net_max(void) {
    uint32_t m = 12500;                                    /* at least 100 Kbps across */
    for (int i = 0; i < g_hist_n; i++) if (g_net_hist[i] > m) m = g_net_hist[i];
    return m + m / 5;
}

static void dev_name(int d, char* out, int cap) {
    static const char* const N[D_COUNT] = { "CPU", "Memory", "Disk", "Ethernet", "GPU" };
    kstrlcpy(out, N[d], (size_t)cap);
}

static void dev_value(int d, char* out, int cap) {
    char a[24], b[24];
    meminfo_t m;
    switch (d) {
    case D_CPU: ksnprintf(out, (size_t)cap, "%d%%", last(g_cpu_hist)); break;
    case D_MEM:
        meminfo_get(&m);
        human_kb(m.used_kb - m.cached_kb, a, sizeof(a));
        human_kb(m.total_kb, b, sizeof(b));
        ksnprintf(out, (size_t)cap, "%s/%s (%d%%)", a, b, last(g_mem_hist));
        break;
    case D_DISK: ksnprintf(out, (size_t)cap, "%d%%", last(g_disk_hist)); break;
    case D_NET:
        if (!net_dev()) { kstrlcpy(out, "Not connected", (size_t)cap); break; }
        bits(g_tx_rate, a, sizeof(a));
        bits(g_rx_rate, b, sizeof(b));
        ksnprintf(out, (size_t)cap, "S: %s R: %s", a, b);
        break;
    default: { gpu_t* g = gpu_active(); kstrlcpy(out, g ? g->driver : "framebuffer", (size_t)cap); }
    }
}

static uint32_t dev_color(int d) {
    static const uint32_t C[D_COUNT] = { C_CPU, C_MEM, C_DISK, C_NET, C_GPU };
    return C[d];
}

/* a stat: its label small, its value under it */
static void stat(int x, int y, const char* label, const char* value) {
    gfx_draw_text(x, y, label, C_DIM, C_BG);
    gfx_draw_text_scaled(x, y + 12, 2, value, C_TEXT, C_BG);
}

static void kv(int x, int y, int kw, const char* k, const char* v) {
    gfx_draw_text(x, y, k, C_DIM, C_BG);
    gfx_draw_text(x + kw, y, v, C_TEXT, C_BG);
}

/* the chosen device's numbers: big ones in a grid (two or three to a row),
 * then the details as "key: value" lines - beside them in a wide window,
 * under them in a narrow one */
static int g_px, g_pw, g_sy, g_sc, g_kx, g_ky, g_kw;

static void layout_stats(int px, int pw, int sy, int big_rows) {
    g_px = px; g_pw = pw; g_sy = sy;
    g_sc = pw >= 720 ? 170 : pw >= 470 ? pw / 3 : pw / 2;
    if (pw >= 720) { g_kx = px + pw - 260; g_ky = sy; }
    else { g_kx = px; g_ky = sy + big_rows * 44 + 6; }
    g_kw = 172;
}
static void bigstat(int col, int row, const char* label, const char* value) {
    int per = g_pw >= 720 ? 3 : g_pw >= 470 ? 3 : 2;
    if (col >= per) { row += col / per; col %= per; }
    stat(g_px + col * g_sc, g_sy + row * 44, label, value);
}
static void kvl(int i, const char* k, const char* v) {
    if (g_ky + i * 14 > g_win.y + g_win.h - 16) return;        /* (no room left) */
    kv(g_kx, g_ky + i * 14, g_kw, k, v);
}

static void draw_performance(void) {
    int cx = content_x(), cw = content_w(), ty = head_y() + 40;
    page_title("Performance");
    /* the tiles */
    for (int d = 0; d < D_COUNT; d++) {
        int y = ty + d * 62, on = d == g_dev;
        uint32_t bg = on ? C_HOVER : C_BG;
        kbnav_add(&g_nav, cx, y, 196, 56);
        gfx_fill_rect(cx, y, 196, 56, bg);
        if (on) gfx_fill_rect(cx, y + 14, 3, 28, C_ACCENT);
        uint32_t c = dev_color(d);
        if (d == D_GPU) frame(cx + 8, y + 8, 64, 40, C_PANEL, c);
        else if (d == D_NET) big_graph(cx + 8, y + 8, 64, 40, NULL, g_net_hist, net_max(), c);
        else big_graph(cx + 8, y + 8, 64, 40, d == D_CPU ? g_cpu_hist : d == D_MEM ? g_mem_hist : g_disk_hist, NULL, 100, c);
        char n[24], v[48];
        dev_name(d, n, sizeof(n));
        dev_value(d, v, sizeof(v));
        if ((int)strlen(v) > 14) v[14] = 0;
        gfx_draw_text(cx + 80, y + 16, n, C_TEXT, bg);
        gfx_draw_text(cx + 80, y + 32, v, C_DIM, bg);
    }

    /* the one chosen: its name and model, its graph, its numbers */
    int px = cx + 210, pw = cw - 210;
    int gy = ty + 36, gh = (g_win.y + g_win.h - gy) * 2 / 5;
    if (gh < 70) gh = 70;
    char line[96], a[24], b[24];
    char nm[24];
    dev_name(g_dev, nm, sizeof(nm));
    gfx_draw_text_scaled(px, ty - 6, 2, nm, C_TEXT, C_BG);
    const sysinfo_t* si = sysinfo_get();
    const char* sub = "";
    meminfo_t m;
    meminfo_get(&m);
    char subbuf[64];
    if (g_dev == D_CPU) sub = si && si->cpu_brand[0] ? si->cpu_brand : "Processor";
    else if (g_dev == D_MEM) { human_kb(m.installed_kb ? m.installed_kb : m.total_kb, a, sizeof(a)); ksnprintf(subbuf, sizeof(subbuf), "%s installed", a); sub = subbuf; }
    else if (g_dev == D_NET) sub = net_dev() ? net_dev()->model : "No adapter";
    else if (g_dev == D_GPU) { gpu_t* g = gpu_active(); sub = g ? g->name : "VBE / UEFI framebuffer"; }
    else sub = "All disks";
    char subt[64];
    kstrlcpy(subt, sub, sizeof(subt));
    while (subt[0] == ' ') memmove(subt, subt + 1, strlen(subt));
    int maxc = (pw - 110) / 8;
    if (maxc > 0 && (int)strlen(subt) > maxc) subt[maxc] = 0;
    gfx_draw_text(px + pw - (int)strlen(subt) * 8, ty + 2, subt, C_DIM, C_BG);

    uint32_t col = dev_color(g_dev);
    int sy = gy + gh + 22;
    uint32_t up = timer_ms() / 1000;
    if (g_dev == D_CPU) {
        int cores = cpu_count() < 1 ? 1 : cpu_count();
        if (g_cores_view && cores > 1) {
            gfx_draw_text(px, gy - 14, "Logical processors", C_DIM, C_BG);
            int n = cores > MAX_CORE ? MAX_CORE : cores, per = n <= 4 ? n : (n + 1) / 2, rows = (n + per - 1) / per;
            int cw2 = (pw - (per - 1) * 6) / per, ch2 = (gh - (rows - 1) * 6) / rows;
            for (int i = 0; i < n; i++) big_graph(px + (i % per) * (cw2 + 6), gy + (i / per) * (ch2 + 6), cw2, ch2, g_core_hist[i], NULL, 100, col);
        } else {
            gfx_draw_text(px, gy - 14, "% Utilization", C_DIM, C_BG);
            gfx_draw_text(px + pw - 32, gy - 14, "100%", C_DIM, C_BG);
            big_graph(px, gy, pw, gh, g_cpu_hist, NULL, 100, col);
        }
        if (cores > 1) gfx_draw_text(px, gy + gh + 2, g_cores_view ? "Click the graph: overall" : "Click the graph: each core", 0x00707070u, C_BG);
        int threads = 0;
        snap_tasks();
        for (int i = 0; i < g_ntasks; i++) if (g_tasks[i].state != TASK_UNUSED) threads++;
        layout_stats(px, pw, sy, 2);
        ksnprintf(a, sizeof(a), "%d%%", last(g_cpu_hist));
        bigstat(0, 0, "Utilization", a);
        ksnprintf(a, sizeof(a), "%d", app_count());
        bigstat(1, 0, "Apps", a);
        ksnprintf(a, sizeof(a), "%d", threads);
        bigstat(2, 0, "Tasks", a);
        ksnprintf(a, sizeof(a), "%u:%02u:%02u:%02u", up / 86400, up / 3600 % 24, up / 60 % 60, up % 60);
        bigstat(0, g_pw >= 470 ? 1 : 2, "Up time", a);
        uint32_t l1, l2, l3;
        caches(&l1, &l2, &l3);
        if (g_pw < 470) g_ky += 44;
        int k = 0;
        ksnprintf(a, sizeof(a), "%d", smp_phys_cores() > 0 ? smp_phys_cores() : cores);
        kvl(k++, "Cores:", a);
        ksnprintf(a, sizeof(a), "%d", cores);
        kvl(k++, "Logical processors:", a);
        kvl(k++, "Virtual machine:", in_vm() ? "Yes" : "No");
        if (l1) { ksnprintf(a, sizeof(a), "%u KB", l1); kvl(k++, "L1 cache:", a); }
        if (l2) { human_kb(l2, a, sizeof(a)); kvl(k++, "L2 cache:", a); }
        if (l3) { human_kb(l3, a, sizeof(a)); kvl(k++, "L3 cache:", a); }
        int temp = sensors_cpu_temp();
        if (temp != SENSOR_NONE) { ksnprintf(a, sizeof(a), "%d C", temp); kvl(k++, "Temperature:", a); }
    } else if (g_dev == D_MEM) {
        gfx_draw_text(px, gy - 14, "Memory usage", C_DIM, C_BG);
        human_kb(m.total_kb, a, sizeof(a));
        gfx_draw_text(px + pw - (int)strlen(a) * 8, gy - 14, a, C_DIM, C_BG);
        big_graph(px, gy, pw, gh - 34, g_mem_hist, NULL, 100, col);
        /* the composition: in use | cached files | free */
        int by = gy + gh - 18;
        gfx_draw_text(px, by - 12, "Memory composition", C_DIM, C_BG);
        frame(px, by, pw, 18, C_PANEL, col);
        int u = m.total_kb ? (int)((uint64_t)(m.used_kb - m.cached_kb) * (uint32_t)(pw - 2) / m.total_kb) : 0;
        int kc = m.total_kb ? (int)((uint64_t)m.cached_kb * (uint32_t)(pw - 2) / m.total_kb) : 0;
        gfx_fill_rect(px + 1, by + 1, u, 16, mix(C_PANEL, col, 150));
        gfx_fill_rect(px + 1 + u, by + 1, kc, 16, mix(C_PANEL, col, 60));
        zswap_stats_t zs;
        zswap_stats(&zs);
        layout_stats(px, pw, sy, 2);
        human_kb(m.used_kb - m.cached_kb, a, sizeof(a));
        bigstat(0, 0, "In use", a);
        human_kb(m.avail_kb, a, sizeof(a));
        bigstat(1, 0, "Available", a);
        human_kb(m.cached_kb, a, sizeof(a));
        bigstat(2, 0, "Cached", a);
        int k = 0;
        human_kb(zs.pages * 4u, a, sizeof(a));
        human_kb((uint32_t)(zs.bytes / 1024u), b, sizeof(b));
        ksnprintf(line, sizeof(line), "%s in %s", a, b);
        kvl(k++, "Compressed:", line);
        human_kb(m.kernel_kb, a, sizeof(a));
        kvl(k++, "Kernel:", a);
        human_kb(m.files_kb, a, sizeof(a));
        kvl(k++, "File data:", a);
        human_kb(m.free_kb, a, sizeof(a));
        kvl(k++, "Free:", a);
        human_kb(m.largest_kb, a, sizeof(a));
        kvl(k++, "Largest block:", a);
        human_kb(zs.disk_pages * 4u, a, sizeof(a));
        kvl(k++, "Swapped out:", zswap_disk_bytes() ? a : "no swap area");
    } else if (g_dev == D_DISK) {
        gfx_draw_text(px, gy - 14, "Active time", C_DIM, C_BG);
        gfx_draw_text(px + pw - 32, gy - 14, "100%", C_DIM, C_BG);
        big_graph(px, gy, pw, gh, g_disk_hist, NULL, 100, col);
        layout_stats(px, pw, sy, 2);
        ksnprintf(a, sizeof(a), "%d%%", last(g_disk_hist));
        bigstat(0, 0, "Active time", a);
        rate(g_rd_rate, a, sizeof(a));
        bigstat(1, 0, "Read speed", a);
        rate(g_wr_rate, a, sizeof(a));
        bigstat(2, 0, "Write speed", a);
        uint64_t rd, wr;
        disk_io_stats(&rd, &wr, NULL);
        int k = 0;
        human_bytes64(rd, a, sizeof(a));
        kvl(k++, "Read since start:", a);
        human_bytes64(wr, a, sizeof(a));
        kvl(k++, "Written:", a);
        for (int i = 0; i < BLOCKDEV_MAX && k < 8; i++) {
            blockdev_t* bd = blockdev_get(i);
            if (!bd || !bd->present) continue;
            human_bytes64((uint64_t)bd->sectors * (bd->sector_size ? bd->sector_size : 512u), a, sizeof(a));
            char model[20];
            kstrlcpy(model, bd->model, sizeof(model));
            ksnprintf(line, sizeof(line), "%s%s", a, bd->system ? ", system" : "");
            char key[24];
            ksnprintf(key, sizeof(key), "%s:", bd->name);
            kvl(k++, key, line);
        }
    } else if (g_dev == D_NET) {
        netdev_t* nd = net_dev();
        gfx_draw_text(px, gy - 14, "Throughput", C_DIM, C_BG);
        bits(net_max(), a, sizeof(a));
        gfx_draw_text(px + pw - (int)strlen(a) * 8, gy - 14, a, C_DIM, C_BG);
        big_graph(px, gy, pw, gh, NULL, g_net_hist, net_max(), col);
        layout_stats(px, pw, sy, 1);
        bits(g_tx_rate, a, sizeof(a));
        bigstat(0, 0, "Send", a);
        bits(g_rx_rate, a, sizeof(a));
        bigstat(1, 0, "Receive", a);
        netif_t* nif = net_if();
        int k = 0;
        kvl(k++, "Adapter name:", nd ? nd->ifname : "none");
        char ip[16] = "-";
        if (nd && nif->configured) ip4_to_str(nif->ip, ip);
        kvl(k++, "IPv4 address:", ip);
        if (nd) {
            ksnprintf(a, sizeof(a), "%02x:%02x:%02x:%02x:%02x:%02x", nd->mac[0], nd->mac[1], nd->mac[2], nd->mac[3], nd->mac[4], nd->mac[5]);
            kvl(k++, "MAC address:", a);
            human_kb(nd->tx_bytes / 1024, a, sizeof(a));
            kvl(k++, "Sent:", a);
            human_kb(nd->rx_bytes / 1024, a, sizeof(a));
            kvl(k++, "Received:", a);
            ksnprintf(a, sizeof(a), "%u errors, %u dropped", nd->rx_errors + nd->tx_errors, nd->rx_dropped);
            kvl(k++, "Problems:", a);
        }
    } else {
        gpu_t* g = gpu_active();
        const fb_info_t* fi = fb_info();
        frame(px, gy, pw, gh, C_PANEL, col);
        gfx_draw_text(px + 16, gy + gh / 2 - 4, "Its load is not measured.", C_DIM, C_PANEL);
        layout_stats(px, pw, sy, 0);
        g_kx = px; g_ky = sy;
        int k = 0;
        kvl(k++, "Adapter:", g ? g->name : "VBE / UEFI framebuffer");
        kvl(k++, "Driver:", g ? g->driver : "none (firmware's mode)");
        if (fi) {
            ksnprintf(a, sizeof(a), "%u x %u, %u-bit", fi->width, fi->height, fi->bpp);
            kvl(k++, "Screen:", a);
            human_kb(fi->pitch * fi->height / 1024, a, sizeof(a));
            kvl(k++, "Framebuffer:", a);
        }
        kvl(k++, "Sound:", audio_device_name());
    }
}

static void draw_details(void) {
    int cx = content_x(), cw = content_w();
    page_title("Details");
    gfx_draw_text(cx + cw - 39 * 8, head_y() + 10, "Right-click a task: its priority", C_DIM, C_BG);
    snap_tasks();
    gfx_fill_rect(cx, table_y() + 12, cw, ROW_H, C_PANEL);
    gfx_draw_text(cx + 8, table_y() + 18, "Name                    PID  Status     Core  CPU   CPU time  Prio", C_DIM, C_PANEL);
    int row = 0, maxr = rows_max(), skip = g_scroll;
    char line[128];
    for (int i = 0; i < g_ntasks && row < maxr; i++) {
        if (g_tasks[i].state == TASK_UNUSED) continue;
        if (skip > 0) { skip--; continue; }
        uint32_t s = g_tasks[i].ticks_total / 100;
        char st[16], core[8];
        kstrlcpy(st, g_tasks[i].state == TASK_AWAY ? "Running" : task_state_str(g_tasks[i].state), sizeof(st));
        if (g_tasks[i].cpu >= 0) ksnprintf(core, sizeof(core), "%d", g_tasks[i].cpu); else kstrlcpy(core, "-", sizeof(core));
        ksnprintf(line, sizeof(line), "%-23s %-4u %-10s %-4s %3u%%  %3u:%02u:%02u  %3d", g_tasks[i].name, g_tasks[i].pid, st, core,
                  g_tasks[i].cpu_pct, s / 3600, s / 60 % 60, s % 60, g_tasks[i].nice);
        int ry = rows_y() + row * ROW_H;
        if (g_tasks[i].cpu_pct) gfx_fill_rect(cx, ry, cw, ROW_H - 1, heat((int)g_tasks[i].cpu_pct));
        gfx_draw_text(cx + 8, ry + 6, line, g_tasks[i].cpu_pct >= 50 ? 0x00FFD080u : C_TEXT, g_tasks[i].cpu_pct ? heat((int)g_tasks[i].cpu_pct) : C_BG);
        row++;
    }
}

static void draw_users(void) {
    int cx = content_x(), cw = content_w();
    page_title("Users");
    gfx_fill_rect(cx, table_y() + 12, cw, ROW_H, C_PANEL);
    gfx_draw_text(cx + 8, table_y() + 18, "User", C_DIM, C_PANEL);
    gfx_draw_text(cx + cw - 300, table_y() + 18, "Status", C_DIM, C_PANEL);
    gfx_draw_text(cx + cw - 180, table_y() + 18, "CPU", C_DIM, C_PANEL);
    gfx_draw_text(cx + cw - 100, table_y() + 18, "Memory", C_DIM, C_PANEL);
    int ry = rows_y();
    frame(cx + 10, ry + 2, 16, 16, 0x00405478u, 0x0090B8F0u);
    gfx_draw_text(cx + 14, ry + 6, "b", C_TEXT, 0x00405478u);
    gfx_draw_text(cx + 34, ry + 6, "banana (you)", C_TEXT, C_BG);
    char a[24];
    ksnprintf(a, sizeof(a), "%d app%s", g_napps, g_napps == 1 ? "" : "s");
    gfx_draw_text(cx + cw - 300, ry + 6, a, C_DIM, C_BG);
    ksnprintf(a, sizeof(a), "%d%%", last(g_cpu_hist));
    gfx_fill_rect(cx + cw - 186, ry, 72, ROW_H - 1, heat(last(g_cpu_hist)));
    gfx_draw_text(cx + cw - 180, ry + 6, a, C_TEXT, heat(last(g_cpu_hist)));
    meminfo_t m;
    meminfo_get(&m);
    human_kb(m.used_kb - m.cached_kb, a, sizeof(a));
    gfx_fill_rect(cx + cw - 106, ry, 100, ROW_H - 1, heat(last(g_mem_hist)));
    gfx_draw_text(cx + cw - 100, ry + 6, a, C_TEXT, heat(last(g_mem_hist)));
    gfx_draw_text(cx + 34, ry + 30, "Banana OS has one user; SSH sessions run as it too.", C_DIM, C_BG);
}

void taskmgr_draw(const fb_info_t* fi) {
    (void)fi;
    if (!g_open) return;
    int x = g_win.x, y = g_win.y, W = g_win.w, H = g_win.h;
    kbnav_begin(&g_nav);
    frame(x, y, W, H, C_BG, 0x00454545u);
    gfx_fill_rect(x + 1, y + 1, W - 2, TITLE_H + 1, C_RAIL);
    /* the title: a small chart icon, the name */
    gfx_fill_rect(x + 10, y + 13, 3, 4, C_ACCENT);
    gfx_fill_rect(x + 14, y + 9, 3, 8, C_ACCENT);
    gfx_fill_rect(x + 18, y + 6, 3, 11, C_ACCENT);
    gfx_draw_text(x + 28, y + 8, "Task Manager", C_TEXT, C_RAIL);
    win_draw_buttons(&g_win, 4, 12);
    draw_rail();
    if (g_page == P_PROCS) draw_processes();
    else if (g_page == P_PERF) draw_performance();
    else if (g_page == P_DETAILS) draw_details();
    else draw_users();
    if (g_status[0]) {
        int sw = (int)strlen(g_status) * 8 + 16;
        gfx_fill_rect(x + W - sw - 10, y + H - 26, sw, 18, C_PANEL);
        gfx_draw_text(x + W - sw - 2, y + H - 21, g_status, C_DIM, C_PANEL);
    }
    gfx_draw_grip(x + W, y + H);
    kbnav_draw(&g_nav, C_ACCENT);
}

/* the window manager (kernel/gui.c) moves, snaps and maximizes it through its geometry */
win_geom_t* taskmgr_geom(void) { return &g_win; }
