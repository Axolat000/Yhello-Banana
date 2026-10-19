/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fb.h"
#include "kstring.h"
#include "gpu.h"
#include "gpu_surface.h"
#include "timer.h"
#include "kheap.h"

/* Multiboot2 info parsing (only what we need) */
typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed)) mb2_info_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

/* Tag type 8: framebuffer */
typedef struct {
    uint32_t type;   /* 8 */
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;
    uint16_t reserved;
    /* followed by color info; ignored */
} __attribute__((packed)) mb2_tag_fb_t;

static fb_info_t g_fb;
static int g_fb_ok = 0;
static uint32_t* g_bb = NULL;
static uint32_t g_bb_w = 0, g_bb_h = 0;
static uint32_t* g_previous;
static uint32_t g_previous_w, g_previous_h;
static int g_previous_valid;
static void forget_damage(void) {
    kfree(g_previous); g_previous = NULL; g_previous_valid = 0;
    g_previous_w = g_previous_h = 0;
}

/* the sizes the firmware offers (Banana Boot's tag 0xBA00): Settings >
 * Screen, on displays without a graphics driver */
#define BOOT_MODES_MAX 48
static uint16_t g_boot_w[BOOT_MODES_MAX], g_boot_h[BOOT_MODES_MAX];
static int      g_boot_n;

int fb_boot_modes(int* w, int* h, int max) {
    int n = g_boot_n < max ? g_boot_n : max;
    for (int i = 0; i < n; i++) { w[i] = g_boot_w[i]; h[i] = g_boot_h[i]; }
    return n;
}

static uint32_t align_up(uint32_t v, uint32_t a) {
    return (v + (a - 1u)) & ~(a - 1u);
}

int fb_init_multiboot2(uint32_t mb2_info_addr) {
    g_fb_ok = 0;
    g_fb.width = g_fb.height = g_fb.pitch = 0;
    g_fb.bpp = 0;
    g_fb.type = 0;
    g_fb.addr = 0;

    if (!mb2_info_addr) return 0;

    mb2_info_t* info = (mb2_info_t*)(uintptr_t)mb2_info_addr;
    uint32_t total = info->total_size;
    uint32_t off = 8;

    while (off + 8 <= total) {
        mb2_tag_t* tag = (mb2_tag_t*)(uintptr_t)(mb2_info_addr + off);
        if (tag->type == 0) break;

        if (tag->type == 0xBA00u && tag->size >= 12) {
            const uint8_t* t = (const uint8_t*)tag;
            uint32_t n = *(const uint32_t*)(t + 8);
            if (n > BOOT_MODES_MAX) n = BOOT_MODES_MAX;
            if (12 + 4 * n > tag->size) n = (tag->size - 12) / 4;
            for (uint32_t i = 0; i < n; i++) {
                g_boot_w[i] = *(const uint16_t*)(t + 12 + 4 * i);
                g_boot_h[i] = *(const uint16_t*)(t + 14 + 4 * i);
            }
            g_boot_n = (int)n;
        }

        if (tag->type == 8 && tag->size >= sizeof(mb2_tag_fb_t)) {
            mb2_tag_fb_t* fb = (mb2_tag_fb_t*)tag;
            g_fb.addr = (uintptr_t)fb->addr; /* identity-mapped assumption */
            g_fb.pitch = fb->pitch;
            g_fb.width = fb->width;
            g_fb.height = fb->height;
            g_fb.bpp = fb->bpp;
            g_fb.type = fb->fb_type;

            if (g_fb.addr && g_fb.width && g_fb.height && (g_fb.bpp == 32) && (g_fb.type == 1))
                g_fb_ok = 1;                   /* (the tags after it are read too) */
        }

        /* Every valid tag is at least 8 bytes (its own type+size header).
         * Guard against a malformed/zero-size tag stalling the scan forever. */
        uint32_t adv = align_up(tag->size, 8);
        if (adv < 8) adv = 8;
        off += adv;
    }

    return g_fb_ok;
}

int fb_available(void) {
    return g_fb_ok;
}

const fb_info_t* fb_info(void) {
    return &g_fb;
}

void fb_putpixel_direct(int x, int y, uint32_t rgb) {
    if (!g_fb_ok) return;
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;

    uintptr_t row = g_fb.addr + (uintptr_t)((uint32_t)y * g_fb.pitch);
    uint32_t* p = (uint32_t*)(row + (uintptr_t)((uint32_t)x * 4u));
    *p = rgb; /* XRGB8888 */
    gpu_flush(x, y, 1, 1);
}

void fb_putpixel(int x, int y, uint32_t rgb) {
    if (!g_fb_ok) return;
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;

    if (g_bb) {
        if ((uint32_t)x < g_bb_w && (uint32_t)y < g_bb_h) {
            g_bb[(uint32_t)y * g_bb_w + (uint32_t)x] = rgb;
        }
        return;
    }

    fb_putpixel_direct(x, y, rgb);
}

uint32_t* fb_target(int* stride_px, int* w, int* h) {
    if (!g_fb_ok) return NULL;
    if (g_bb) {
        *stride_px = (int)g_bb_w;
        *w = (int)(g_bb_w < g_fb.width ? g_bb_w : g_fb.width);
        *h = (int)(g_bb_h < g_fb.height ? g_bb_h : g_fb.height);
        return g_bb;
    }
    gpu_dirty_raw();
    *stride_px = (int)(g_fb.pitch / 4u);
    *w = (int)g_fb.width;
    *h = (int)g_fb.height;
    return (uint32_t*)g_fb.addr;
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    int stride, tw, th;
    uint32_t* t = fb_target(&stride, &tw, &th);
    if (!t || w <= 0 || h <= 0) return;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > tw) x1 = tw;
    if (y1 > th) y1 = th;
    if (x0 >= x1 || y0 >= y1) return;

    for (int yy = y0; yy < y1; yy++)
        memset32(t + (uint32_t)yy * (uint32_t)stride + (uint32_t)x0, rgb, (uint32_t)(x1 - x0));
}

void fb_scroll_up(int top, int height, int dy, uint32_t fill) {
    int stride, tw, th;
    uint32_t* t = fb_target(&stride, &tw, &th);
    if (!t || dy <= 0 || height <= 0) return;
    if (top + height > th) height = th - top;
    if (dy >= height) { fb_fill_rect(0, top, tw, height, fill); return; }
    /* one block move of every row but the first dy, then clear the gap */
    memmove(t + (uint32_t)top * (uint32_t)stride,
            t + (uint32_t)(top + dy) * (uint32_t)stride,
            (uint32_t)(height - dy) * (uint32_t)stride * 4u);
    fb_fill_rect(0, top + height - dy, tw, dy, fill);
}

void fb_reconfigure(uint32_t width, uint32_t height, uint32_t pitch) {
    forget_damage();
    g_fb.width = width;
    g_fb.height = height;
    g_fb.pitch = pitch;
}

void fb_set_framebuffer(uintptr_t addr, uint32_t width, uint32_t height, uint32_t pitch) {
    g_fb.addr = addr;
    forget_damage();
    g_fb.width = width;
    g_fb.height = height;
    g_fb.pitch = pitch;
    g_fb.bpp = 32;
    g_fb.type = 1;
    g_fb_ok = addr && width && height;
}

void fb_set_backbuffer(uint32_t* buf, uint32_t buf_width, uint32_t buf_height) {
    forget_damage();
    g_bb = buf;
    g_bb_w = buf_width;
    g_bb_h = buf_height;
}

void fb_clear_backbuffer(void) {
    forget_damage();
    g_bb = NULL;
    g_bb_w = g_bb_h = 0;
}

void fb_present_rect(int x, int y, int w, int h) {
    if (!g_fb_ok || !g_bb) return;
    gpu_surface_t dst = {(uint32_t*)g_fb.addr, (int)g_fb.width, (int)g_fb.height, (int)(g_fb.pitch / 4)};
    gpu_surface_t src = {g_bb, (int)g_bb_w, (int)g_bb_h, (int)g_bb_w};
    gpu_rect_t r = {x, y, w, h};
    int width = dst.width < src.width ? dst.width : src.width;
    int height = dst.height < src.height ? dst.height : src.height;
    if (!gpu_rect_clip(&r, width, height)) return;
    uint64_t t = timer_counter();
    uint64_t bytes = gpu_surface_copy(dst, src, r);
    gpu_note_present(bytes, timer_counter() - t);
    gpu_flush(r.x, r.y, r.w, r.h);
}

void fb_present(void) {
    g_previous_valid = 0;
    fb_present_rect(0, 0, (int)g_bb_w, (int)g_bb_h);
}

void fb_scanout_addr(uintptr_t addr) { g_fb.addr = addr; }
void fb_invalidate_damage(void) { g_previous_valid = 0; }
#define PRESENT_RECTS 128
typedef struct {
    gpu_surface_t source, target;
    gpu_rect_t rects[PRESENT_RECTS];
    int flipping, count, overflow;
} present_t;
static void present_damage(gpu_rect_t r, void* opaque) {
    present_t* p = opaque;
    if (p->flipping) {
        uint64_t start = timer_counter();
        uint64_t bytes = gpu_surface_copy(p->target, p->source, r);
        gpu_note_present(bytes, timer_counter() - start);
        if (p->count < PRESENT_RECTS) p->rects[p->count++] = r;
        else p->overflow = 1;
    } else fb_present_rect(r.x, r.y, r.w, r.h);
}
void fb_present_damage(void) {
    if (!g_fb_ok || !g_bb) return;
    uint32_t w = g_bb_w < g_fb.width ? g_bb_w : g_fb.width;
    uint32_t h = g_bb_h < g_fb.height ? g_bb_h : g_fb.height;
    if (!w || !h || (uint64_t)w * h > (size_t)-1 / 4) return;
    if (!g_previous || g_previous_w != w || g_previous_h != h) {
        forget_damage();
        g_previous = kmalloc((size_t)w * h * 4);
        if (!g_previous) { fb_present(); return; }
        g_previous_w = w; g_previous_h = h;
    }
    present_t p = {0};
    p.source = (gpu_surface_t){g_bb, (int)w, (int)h, (int)g_bb_w};
    p.flipping = gpu_present_begin(&p.target);
    if (p.flipping == 2) g_previous_valid = 0;
    if (!p.flipping) gpu_wait_vblank();
    uint64_t copied = gpu_surface_damage(p.source,
        (gpu_surface_t){g_previous, (int)w, (int)h, (int)w}, g_previous_valid, present_damage, &p);
    g_previous_valid = 1;
    if (p.flipping && p.count) {
        if (p.overflow) { p.rects[0] = (gpu_rect_t){0, 0, (int)w, (int)h}; p.count = 1; }
        gpu_present_end(p.source, p.rects, p.count);
    }
    gpu_note_damage((uint64_t)w * h * 4, copied);
}
