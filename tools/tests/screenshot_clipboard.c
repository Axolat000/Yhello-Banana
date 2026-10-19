/* Host unit test for screenshot PNG encoding and multi-format clipboard */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../../web/test/shim/types.h"

/* Freestanding kheap shims using host malloc/free */
void* kmalloc(size_t sz) { return malloc(sz); }
void* kmalloc_aligned(size_t sz, size_t align) { (void)align; return malloc(sz); }
void* krealloc(void* p, size_t sz) { return realloc(p, sz); }
void  kfree(void* p) { free(p); }

/* Mock spinlock */
#define SPINLOCK_H
typedef struct { volatile int v; } spinlock_t;
#define SPINLOCK_INIT { 0 }
static inline void spin_lock(spinlock_t* l) { (void)l; }
static inline void spin_unlock(spinlock_t* l) { (void)l; }
static inline uintptr_t spin_lock_irq(spinlock_t* l) { (void)l; return 0; }
static inline void spin_unlock_irq(spinlock_t* l, uintptr_t f) { (void)l; (void)f; }

/* Mock timer and rtc */
#define TIMER_H
uint32_t timer_ms(void) { return 5000; }

#include "../../kernel/rtc.h"
int rtc_read_datetime(rtc_datetime_t* out) {
    if (out) {
        out->year = 2026;
        out->month = 10;
        out->day = 19;
        out->hour = 17;
        out->minute = 45;
        out->second = 0;
    }
    return 0;
}
uint32_t rtc_now(void) { return 1700000000; }

/* Mock filesystem */
#define FS_H
#define FS_PATH_LEN 256
static char g_mock_file_path[FS_PATH_LEN];
static uint8_t g_mock_file_buf[1024 * 1024];
static uint32_t g_mock_file_len;

int fs_mkdir_p(const char* dir) { (void)dir; return 0; }
int fs_find_file(const char* path) {
    if (strcmp(path, g_mock_file_path) == 0 && g_mock_file_len > 0) return 0;
    return -1;
}
int fs_write_path(const char* path, const char* data, uint32_t len) {
    strncpy(g_mock_file_path, path, sizeof(g_mock_file_path) - 1);
    if (len > sizeof(g_mock_file_buf)) len = sizeof(g_mock_file_buf);
    memcpy(g_mock_file_buf, data, len);
    g_mock_file_len = len;
    return (int)len;
}
int fs_delete(const char* path, int rec) {
    (void)rec;
    if (strcmp(path, g_mock_file_path) == 0) g_mock_file_len = 0;
    return 0;
}
typedef struct { uint32_t size; const char* content; } fs_file_t;
static fs_file_t g_mock_fs_fl;
fs_file_t* fs_get_file(int fi) {
    (void)fi;
    g_mock_fs_fl.size = g_mock_file_len;
    g_mock_fs_fl.content = (const char*)g_mock_file_buf;
    return &g_mock_fs_fl;
}

/* Mock FB and gfx */
#include "../../kernel/fb.h"
#define GFX_H
void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    (void)x; (void)y; (void)w; (void)h; (void)rgb;
}
void gfx_draw_text(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    (void)x; (void)y; (void)s; (void)fg; (void)bg;
}
static uint32_t g_mock_fb[16 * 16];
uint32_t* fb_target(int* stride_px, int* w, int* h) {
    if (stride_px) *stride_px = 16;
    if (w) *w = 16;
    if (h) *h = 16;
    return g_mock_fb;
}

/* Include deflate and implementation */
#include "../../kernel/deflate.c"
#include "../../kernel/notification.c"
#include "../../kernel/screenshot.c"
#include "../../kernel/clipboard.c"

int main(void) {
    notify_init();

    /* ── 1. Test Clipboard Multi-Format (Text, Image, Files) ── */
    /* Text */
    clipboard_set("Hello Antigravity", 17);
    assert(clipboard_get_format() == CLIP_FORMAT_TEXT);
    uint32_t len = 0;
    assert(strcmp(clipboard_get(&len), "Hello Antigravity") == 0);
    assert(len == 17);

    /* Files */
    const char* files[3] = { "/home/banana/file1.txt", "/home/banana/photo.png", "/etc/passwd" };
    assert(clipboard_set_files(files, 3) == 0);
    assert(clipboard_get_format() == CLIP_FORMAT_FILES);
    char fbuf[256] = "";
    int cnt = clipboard_get_files(fbuf, sizeof(fbuf), &len);
    assert(cnt == 3);
    assert(strstr(fbuf, "/home/banana/file1.txt") != NULL);
    assert(strstr(fbuf, "/home/banana/photo.png") != NULL);
    assert(strstr(fbuf, "/etc/passwd") != NULL);

    /* Image */
    static const uint8_t test_img[64] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0xDE, 0xAD, 0xBE, 0xEF
    };
    assert(clipboard_set_image(test_img, sizeof(test_img)) == 0);
    assert(clipboard_get_format() == CLIP_FORMAT_IMAGE);
    uint32_t img_len = 0;
    const void* img_ptr = clipboard_get_image(&img_len);
    assert(img_ptr != NULL);
    assert(img_len == sizeof(test_img));
    assert(memcmp(img_ptr, test_img, sizeof(test_img)) == 0);

    /* History inspection with format types */
    assert(clipboard_hist_count() == 3);
    char hist_item[128] = "";
    clip_format_t fmt = CLIP_FORMAT_TEXT;
    int pinned = 0;

    /* Entry 0: newest (Image) */
    assert(clipboard_hist_peek_ex(0, hist_item, sizeof(hist_item), &len, &pinned, &fmt) == 0);
    assert(fmt == CLIP_FORMAT_IMAGE);
    assert(strstr(hist_item, "[Image:") != NULL);

    /* Entry 1: Files */
    assert(clipboard_hist_peek_ex(1, hist_item, sizeof(hist_item), &len, &pinned, &fmt) == 0);
    assert(fmt == CLIP_FORMAT_FILES);
    assert(strstr(hist_item, "[3 Files:") != NULL);

    /* Entry 2: Text */
    assert(clipboard_hist_peek_ex(2, hist_item, sizeof(hist_item), &len, &pinned, &fmt) == 0);
    assert(fmt == CLIP_FORMAT_TEXT);
    assert(strcmp(hist_item, "Hello Antigravity") == 0);

    /* Test pin and persistence format */
    assert(clipboard_hist_pin(0, 1) == 0);
    assert(g_mock_file_len > 0); /* PINNED_FILE written */

    /* ── 2. Test Screenshot Capture and PNG Encoding ── */
    /* Set mock frame buffer pixels */
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            g_mock_fb[y * 16 + x] = (x < 8) ? 0x00FF0000u : 0x0000FF00u; /* Red / Green */
        }
    }

    screenshot_set_directory("/home/banana/Pictures");
    char dir_check[64] = "";
    assert(strcmp(screenshot_get_directory(dir_check, sizeof(dir_check)), "/home/banana/Pictures") == 0);

    char out_path[FS_PATH_LEN] = "";
    int sc_res = screenshot_take("/home/banana/Pictures", 1, out_path, sizeof(out_path));
    assert(sc_res == 0);
    assert(strstr(out_path, "/home/banana/Pictures/Screenshot-20261019-01.png") != NULL);
    assert(g_mock_file_len > 64);
    assert(memcmp(g_mock_file_buf, "\x89PNG\r\n\x1a\n", 8) == 0);

    /* Verify visible feedback notification was posted */
    assert(notify_count() == 1);
    notify_item_t nitem;
    assert(notify_peek(0, &nitem) == 0);
    assert(strcmp(nitem.app, "Screenshot") == 0);
    assert(strcmp(nitem.title, "Screen Captured") == 0);
    assert(strstr(nitem.body, "Screenshot-20261019-01.png") != NULL);

    /* Low level PNG encode test with 24-bit RGB */
    uint8_t rgb24[3 * 4 * 4];
    memset(rgb24, 0x7F, sizeof(rgb24));
    uint32_t enc_len = 0;
    uint8_t* enc_png = screenshot_encode_png(rgb24, 4, 4, 12, 0, &enc_len);
    assert(enc_png != NULL);
    assert(enc_len > 30);
    assert(memcmp(enc_png, "\x89PNG\r\n\x1a\n", 8) == 0);
    free(enc_png);

    puts("screenshot & clipboard: all host unit tests passed successfully");
    return 0;
}
