#include "clipboard.h"
#include "kheap.h"
#include "kstring.h"
#include "spinlock.h"
#include "fs.h"

static char*         g_data;
static uint32_t      g_len;
static clip_format_t g_format = CLIP_FORMAT_TEXT;
static uint32_t      g_gen;

/* the history: g_hist[0] is the newest */
typedef struct {
    char*         data;
    uint32_t      len;
    clip_format_t format;
    int           pinned;
} clip_item_t;

static clip_item_t g_hist[CLIP_HIST_MAX];
static int         g_nhist;
static uint32_t    g_hgen;
static spinlock_t  g_lock = SPINLOCK_INIT;     /* (apps copy from other cores) */

#define PINNED_FILE "/etc/clipboard-pinned"

/* puts data (kmalloc'd, owned from now on) at the top: a copy already
 * there moves up instead; the oldest entry not pinned goes when it is full.
 * Called with the lock held; what it drops, it returns (kfree it after) */
static char* hist_push(clip_format_t format, char* data, uint32_t len, int pinned) {
    for (int i = 0; i < g_nhist; i++) {
        if (g_hist[i].format != format || g_hist[i].len != len || memcmp(g_hist[i].data, data, len) != 0) continue;
        clip_item_t it = g_hist[i];
        memmove(&g_hist[1], &g_hist[0], (size_t)i * sizeof(clip_item_t));
        it.pinned |= pinned;
        g_hist[0] = it;
        g_hgen++;
        return data;                                     /* (the new copy is not needed) */
    }
    char* drop = NULL;
    if (g_nhist == CLIP_HIST_MAX) {
        int last = g_nhist - 1;
        while (last >= 0 && g_hist[last].pinned) last--;
        if (last < 0) last = g_nhist - 1;                /* (all pinned: the oldest goes) */
        drop = g_hist[last].data;
        memmove(&g_hist[last], &g_hist[last + 1], (size_t)(g_nhist - last - 1) * sizeof(clip_item_t));
        g_nhist--;
    }
    memmove(&g_hist[1], &g_hist[0], (size_t)g_nhist * sizeof(clip_item_t));
    g_hist[0] = (clip_item_t){ data, len, format, pinned };
    g_nhist++;
    g_hgen++;
    return drop;
}

void clipboard_set_data(clip_format_t format, const void* data, uint32_t len) {
    uint32_t max_limit = (format == CLIP_FORMAT_IMAGE) ? CLIPBOARD_IMAGE_MAX : CLIPBOARD_MAX;
    if (len > max_limit) len = max_limit;

    char* t = (char*)kmalloc(len + 1);
    char* h = len ? (char*)kmalloc(len + 1) : NULL;
    if (!t) { kfree(h); return; }
    if (data && len) {
        memcpy(t, data, len);
        if (h) memcpy(h, data, len);
    }
    t[len] = 0;
    if (h) h[len] = 0;

    uintptr_t f = spin_lock_irq(&g_lock);
    char* old = g_data;
    g_data = t;
    g_len = len;
    g_format = format;
    g_gen++;
    char* drop = h ? hist_push(format, h, len, 0) : NULL;
    spin_unlock_irq(&g_lock, f);

    kfree(old);
    kfree(drop);
}

void clipboard_set(const char* text, uint32_t len) {
    clipboard_set_data(CLIP_FORMAT_TEXT, text, len);
}

const void* clipboard_get_data(clip_format_t* format, uint32_t* len) {
    if (format) *format = g_format;
    if (len) *len = g_data ? g_len : 0;
    return g_data ? g_data : "";
}

clip_format_t clipboard_get_format(void) {
    return g_format;
}

const char* clipboard_get(uint32_t* len) {
    if (g_format == CLIP_FORMAT_TEXT) {
        if (len) *len = g_data ? g_len : 0;
        return g_data ? g_data : "";
    }
    if (len) *len = 0;
    return "";
}

int clipboard_set_image(const void* png_data, uint32_t len) {
    if (!png_data || !len) return -1;
    clipboard_set_data(CLIP_FORMAT_IMAGE, png_data, len);
    return 0;
}

const void* clipboard_get_image(uint32_t* len) {
    if (g_format == CLIP_FORMAT_IMAGE) {
        if (len) *len = g_data ? g_len : 0;
        return g_data;
    }
    if (len) *len = 0;
    return NULL;
}

int clipboard_set_files(const char* const* paths, int count) {
    if (!paths || count <= 0) return -1;
    if (count > 64) count = 64;

    uint32_t total = 0;
    for (int i = 0; i < count; i++) {
        if (paths[i]) total += (uint32_t)strlen(paths[i]) + 1u;
    }
    if (total == 0 || total > CLIPBOARD_MAX) return -1;

    char* buf = (char*)kmalloc(total + 1);
    if (!buf) return -1;

    uint32_t off = 0;
    for (int i = 0; i < count; i++) {
        if (!paths[i]) continue;
        uint32_t l = (uint32_t)strlen(paths[i]);
        memcpy(buf + off, paths[i], l);
        off += l;
        buf[off++] = '\n';
    }
    buf[off] = 0;

    clipboard_set_data(CLIP_FORMAT_FILES, buf, off);
    kfree(buf);
    return 0;
}

int clipboard_get_files(char* out_paths, uint32_t cap, uint32_t* len) {
    if (g_format != CLIP_FORMAT_FILES || !g_data) {
        if (len) *len = 0;
        if (out_paths && cap) out_paths[0] = 0;
        return 0;
    }
    if (len) *len = g_len;
    if (out_paths && cap) {
        uint32_t copy_n = g_len < cap - 1 ? g_len : cap - 1;
        memcpy(out_paths, g_data, copy_n);
        out_paths[copy_n] = 0;
    }
    /* Count files by newlines */
    int cnt = 0;
    for (uint32_t i = 0; i < g_len; i++) {
        if (g_data[i] == '\n') cnt++;
    }
    return cnt;
}

uint32_t clipboard_generation(void) { return g_gen; }

/* ── the history ───────────────────────────────────────────────────── */

int clipboard_hist_count(void) { return g_nhist; }
uint32_t clipboard_hist_generation(void) { return g_hgen; }

int clipboard_hist_peek_ex(int i, char* out, uint32_t cap, uint32_t* len, int* pinned, clip_format_t* fmt) {
    int rc = -1;
    uintptr_t f = spin_lock_irq(&g_lock);
    if (i >= 0 && i < g_nhist) {
        if (out && cap) {
            if (g_hist[i].format == CLIP_FORMAT_IMAGE) {
                ksnprintf(out, (size_t)cap, "[Image: %u bytes]", g_hist[i].len);
            } else if (g_hist[i].format == CLIP_FORMAT_FILES) {
                /* Summary first path and count */
                int nfiles = 0;
                char first_path[64] = "";
                const char* p = g_hist[i].data;
                while (*p && *p != '\n' && (size_t)(p - g_hist[i].data) < sizeof(first_path) - 1) {
                    first_path[p - g_hist[i].data] = *p;
                    p++;
                }
                first_path[p - g_hist[i].data] = 0;
                for (uint32_t k = 0; k < g_hist[i].len; k++) if (g_hist[i].data[k] == '\n') nfiles++;
                if (nfiles > 1) {
                    ksnprintf(out, (size_t)cap, "[%d Files: %s ...]", nfiles, first_path);
                } else {
                    ksnprintf(out, (size_t)cap, "[File: %s]", first_path);
                }
            } else {
                uint32_t n = g_hist[i].len < cap - 1 ? g_hist[i].len : cap - 1;
                memcpy(out, g_hist[i].data, n);
                out[n] = 0;
            }
        }
        if (len) *len = g_hist[i].len;
        if (pinned) *pinned = g_hist[i].pinned;
        if (fmt) *fmt = g_hist[i].format;
        rc = 0;
    }
    spin_unlock_irq(&g_lock, f);
    return rc;
}

int clipboard_hist_peek(int i, char* out, uint32_t cap, uint32_t* len, int* pinned) {
    return clipboard_hist_peek_ex(i, out, cap, len, pinned, NULL);
}

static void save_pinned(void);

int clipboard_hist_use(int i) {
    uintptr_t f = spin_lock_irq(&g_lock);
    if (i < 0 || i >= g_nhist) {
        spin_unlock_irq(&g_lock, f);
        return -1;
    }
    clip_format_t fmt = g_hist[i].format;
    uint32_t len = g_hist[i].len;
    char* copy = (char*)kmalloc(len + 1);
    if (copy) {
        memcpy(copy, g_hist[i].data, len);
        copy[len] = 0;
    }
    spin_unlock_irq(&g_lock, f);

    if (!copy) return -1;
    clipboard_set_data(fmt, copy, len);
    kfree(copy);
    return 0;
}

int clipboard_hist_delete(int i) {
    char* drop = NULL;
    int was_pinned = 0;
    uintptr_t f = spin_lock_irq(&g_lock);
    if (i >= 0 && i < g_nhist) {
        drop = g_hist[i].data;
        was_pinned = g_hist[i].pinned;
        memmove(&g_hist[i], &g_hist[i + 1], (size_t)(g_nhist - i - 1) * sizeof(clip_item_t));
        g_nhist--;
        g_hgen++;
    }
    spin_unlock_irq(&g_lock, f);
    if (!drop) return -1;
    kfree(drop);
    if (was_pinned) save_pinned();
    return 0;
}

int clipboard_hist_pin(int i, int on) {
    int rc = -1;
    uintptr_t f = spin_lock_irq(&g_lock);
    if (i >= 0 && i < g_nhist) { g_hist[i].pinned = on != 0; g_hgen++; rc = 0; }
    spin_unlock_irq(&g_lock, f);
    if (rc == 0) save_pinned();
    return rc;
}

int clipboard_hist_clear(void) {
    char* drop[CLIP_HIST_MAX];
    int nd = 0, k = 0;
    uintptr_t f = spin_lock_irq(&g_lock);
    for (int i = 0; i < g_nhist; i++) {
        if (g_hist[i].pinned) g_hist[k++] = g_hist[i];
        else drop[nd++] = g_hist[i].data;
    }
    g_nhist = k;
    if (nd) g_hgen++;
    spin_unlock_irq(&g_lock, f);
    for (int i = 0; i < nd; i++) kfree(drop[i]);
    return nd;
}

/* ── pinned entries across restarts ───────────────────────────────────
 * /etc/clipboard-pinned: for each, format in decimal, a space,
 * length in decimal, a newline, the data bytes, a newline - oldest first */

static void save_pinned(void) {
    uint32_t total = 0;
    int n = 0;
    uintptr_t f = spin_lock_irq(&g_lock);
    for (int i = 0; i < g_nhist; i++) {
        if (g_hist[i].pinned) { total += g_hist[i].len + 24; n++; }
    }
    spin_unlock_irq(&g_lock, f);
    if (!n) { if (fs_find_file(PINNED_FILE) >= 0) fs_delete(PINNED_FILE, 0); return; }
    char* buf = (char*)kmalloc(total + 1);
    if (!buf) return;
    uint32_t off = 0;
    f = spin_lock_irq(&g_lock);
    for (int i = g_nhist - 1; i >= 0; i--) {
        if (!g_hist[i].pinned) continue;
        if (off + g_hist[i].len + 24 > total) break;
        off += (uint32_t)ksnprintf(buf + off, 24, "%d %u\n", (int)g_hist[i].format, g_hist[i].len);
        memcpy(buf + off, g_hist[i].data, g_hist[i].len);
        off += g_hist[i].len;
        buf[off++] = '\n';
    }
    spin_unlock_irq(&g_lock, f);
    fs_mkdir_p("/etc");
    fs_write_path(PINNED_FILE, buf, off);
    kfree(buf);
}

void clipboard_hist_load(void) {
    static int done;
    if (done) return;
    done = 1;
    int fi = fs_find_file(PINNED_FILE);
    fs_file_t* fl = fi >= 0 ? fs_get_file(fi) : NULL;
    if (!fl || !fl->content) return;
    const char* p = fl->content, *end = fl->content + fl->size;
    while (p < end) {
        int fmt_val = 0;
        uint32_t len = 0;
        /* Legacy format was just "<len>\n", new format is "<fmt> <len>\n" */
        while (p < end && *p >= '0' && *p <= '9') {
            len = len * 10u + (uint32_t)(*p++ - '0');
        }
        if (p < end && *p == ' ') {
            p++;
            fmt_val = (int)len;
            len = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                len = len * 10u + (uint32_t)(*p++ - '0');
            }
        }
        uint32_t max_limit = (fmt_val == CLIP_FORMAT_IMAGE) ? CLIPBOARD_IMAGE_MAX : CLIPBOARD_MAX;
        if (p >= end || *p != '\n' || len > max_limit || (uint32_t)(end - p - 1) < len) break;
        p++;
        char* t = (char*)kmalloc(len + 1);
        if (!t) break;
        memcpy(t, p, len);
        t[len] = 0;
        p += len + 1;
        uintptr_t f = spin_lock_irq(&g_lock);
        char* drop = hist_push((clip_format_t)fmt_val, t, len, 1);
        spin_unlock_irq(&g_lock, f);
        kfree(drop);
    }
}
