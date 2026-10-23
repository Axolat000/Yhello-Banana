/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screenshot.h"
#include "fb.h"
#include "fs.h"
#include "kheap.h"
#include "kstring.h"
#include "deflate.h"
#include "timer.h"
#include "rtc.h"

static char g_screenshot_dir[FS_PATH_LEN] = SCREENSHOT_DIR_DEFAULT;

int screenshot_set_directory(const char* dir) {
    if (!dir || dir[0] != '/' || strlen(dir) > FS_PATH_LEN - 48) return -1;
    kstrlcpy(g_screenshot_dir, dir, sizeof(g_screenshot_dir));
    /* Strip trailing slash if present (except root) */
    size_t len = strlen(g_screenshot_dir);
    if (len > 1 && g_screenshot_dir[len - 1] == '/') {
        g_screenshot_dir[len - 1] = '\0';
    }
    return 0;
}

const char* screenshot_get_directory(char* out, int cap) {
    if (out && cap > 0) {
        kstrlcpy(out, g_screenshot_dir, (size_t)cap);
    }
    return g_screenshot_dir;
}

/* Adler-32 checksum computation for zlib header */
static uint32_t adler32_calc(const uint8_t* data, uint32_t len) {
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    for (uint32_t i = 0; i < len; i++) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

static inline void write_be32(uint8_t* p, uint32_t val) {
    p[0] = (uint8_t)(val >> 24);
    p[1] = (uint8_t)(val >> 16);
    p[2] = (uint8_t)(val >> 8);
    p[3] = (uint8_t)(val);
}

/*
 * Low-level PNG encoder: encodes 24-bit RGB or 32-bit XRGB/BGRX pixels
 * into standard PNG file bytes.
 */
uint8_t* screenshot_encode_png(const uint8_t* pixels, int w, int h, int stride_bytes, int is_bgrx, uint32_t* out_len) {
    if (!out_len) return NULL;
    *out_len = 0;
    if (!pixels || w <= 0 || h <= 0 || stride_bytes <= 0) return NULL;
    uint64_t source_row = (uint64_t)(uint32_t)w * (is_bgrx ? 4u : 3u);
    uint64_t raw_bytes = (1ull + (uint64_t)(uint32_t)w * 3u) * (uint32_t)h;
    /* Bound temporary memory and avoid arithmetic/address wrap on i386. */
    if (source_row > (uint32_t)stride_bytes || raw_bytes > (64u << 20) ||
        (uint64_t)(uint32_t)stride_bytes * (uint32_t)h > (uint64_t)(size_t)-1) return NULL;

    /* Raw filtered scanlines: 1 filter byte + 3 bytes RGB per pixel */
    uint32_t raw_row_len = 1u + (uint32_t)w * 3u;
    uint32_t raw_size = raw_row_len * (uint32_t)h;
    uint8_t* raw_buf = (uint8_t*)kmalloc(raw_size);
    if (!raw_buf) return NULL;

    for (int y = 0; y < h; y++) {
        uint8_t* dst_row = raw_buf + (uint32_t)y * raw_row_len;
        *dst_row++ = 0; /* filter type 0: None */
        const uint8_t* src_row = pixels + (size_t)y * (size_t)stride_bytes;

        if (is_bgrx) {
            /* Input is 32-bit: B, G, R, X (or little-endian 0x00RRGGBB) */
            for (int x = 0; x < w; x++) {
                uint8_t b = src_row[x * 4 + 0];
                uint8_t g = src_row[x * 4 + 1];
                uint8_t r = src_row[x * 4 + 2];
                dst_row[x * 3 + 0] = r;
                dst_row[x * 3 + 1] = g;
                dst_row[x * 3 + 2] = b;
            }
        } else {
            /* Input is already 24-bit RGB */
            memcpy(dst_row, src_row, (size_t)w * 3u);
        }
    }

    /* Compute Adler32 checksum over raw uncompressed scanlines */
    uint32_t adler = adler32_calc(raw_buf, raw_size);

    /* Compress raw scanlines with DEFLATE */
    uint32_t def_len = 0;
    uint8_t* def_buf = deflate_compress(raw_buf, raw_size, &def_len);
    kfree(raw_buf);
    if (!def_buf) return NULL;

    /* Zlib stream: CMF (0x78) + FLG (0x01) + def_buf + Adler32 (4 bytes) */
    uint32_t zlib_len = 2u + def_len + 4u;
    uint8_t* zlib_buf = (uint8_t*)kmalloc(zlib_len);
    if (!zlib_buf) {
        kfree(def_buf);
        return NULL;
    }
    zlib_buf[0] = 0x78;
    zlib_buf[1] = 0x01;
    memcpy(zlib_buf + 2, def_buf, def_len);
    kfree(def_buf);
    write_be32(zlib_buf + 2 + def_len, adler);

    /*
     * Build complete PNG:
     * - 8 bytes Signature: 89 50 4E 47 0D 0A 1A 0A
     * - IHDR chunk: 4 (len=13) + 4 (type) + 13 (data) + 4 (crc) = 25 bytes
     * - IDAT chunk: 4 (len) + 4 (type) + zlib_len (data) + 4 (crc) = 12 + zlib_len
     * - IEND chunk: 4 (len=0) + 4 (type) + 0 + 4 (crc) = 12 bytes
     */
    uint32_t total_png = 8u + 25u + (12u + zlib_len) + 12u;
    uint8_t* png = (uint8_t*)kmalloc(total_png);
    if (!png) {
        kfree(zlib_buf);
        return NULL;
    }

    uint8_t* p = png;

    /* Signature */
    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    memcpy(p, signature, 8);
    p += 8;

    /* IHDR Chunk */
    write_be32(p, 13);
    p += 4;
    memcpy(p, "IHDR", 4);
    p += 4;
    write_be32(p + 0, (uint32_t)w);
    write_be32(p + 4, (uint32_t)h);
    p[8] = 8; /* bit depth 8 */
    p[9] = 2; /* color type 2: RGB */
    p[10] = 0; /* compression 0 */
    p[11] = 0; /* filter 0 */
    p[12] = 0; /* interlace 0 */
    uint32_t crc_ihdr = crc32_update(0, p - 4, 4 + 13);
    p += 13;
    write_be32(p, crc_ihdr);
    p += 4;

    /* IDAT Chunk */
    write_be32(p, zlib_len);
    p += 4;
    memcpy(p, "IDAT", 4);
    p += 4;
    memcpy(p, zlib_buf, zlib_len);
    uint32_t crc_idat = crc32_update(0, p - 4, 4);
    crc_idat = crc32_update(crc_idat, zlib_buf, zlib_len);
    kfree(zlib_buf);
    p += zlib_len;
    write_be32(p, crc_idat);
    p += 4;

    /* IEND Chunk */
    write_be32(p, 0);
    p += 4;
    memcpy(p, "IEND", 4);
    p += 4;
    uint32_t crc_iend = crc32_update(0, p - 4, 4);
    write_be32(p, crc_iend);
    p += 4;

    *out_len = (uint32_t)(p - png);
    return png;
}

/* Serialized capture requests; the filesystem remains responsible for writes.
 * Name probing has a finite bound and never falls back to an existing name. */
static volatile int g_capture_busy;
int screenshot_take(const char* target_dir, char* out_path, int cap) {
    if (out_path && cap > 0) out_path[0] = 0;
    if (__sync_lock_test_and_set(&g_capture_busy, 1)) return -1;
    int rc = -1, stride = 0, w = 0, h = 0;
    uint8_t* png = NULL;
    uint32_t* fb = fb_target(&stride, &w, &h);
    if (!fb || stride <= 0 || stride > 0x1fffffff || w <= 0 || h <= 0) goto done;
    const char* dir = target_dir && *target_dir ? target_dir : g_screenshot_dir;
    if (dir[0] != '/' || strlen(dir) > FS_PATH_LEN - 48 || fs_mkdir_p(dir) < 0) goto done;
    uint32_t len = 0;
    png = screenshot_encode_png((const uint8_t*)fb, w, h, stride * 4, 1, &len);
    if (!png) goto done;
    rtc_datetime_t dt;
    int dated = rtc_read_datetime(&dt) == 0 && dt.year >= 2000;
    char path[FS_PATH_LEN];
    int found = 0;
    for (unsigned n = 1; n <= 9999; n++) {
        if (dated) ksnprintf(path, sizeof(path), "%s/Screenshot-%04d%02d%02d-%04u.png",
                             dir, dt.year, dt.month, dt.day, n);
        else ksnprintf(path, sizeof(path), "%s/Screenshot-%04u.png", dir, n);
        if (fs_find_file(path) < 0 && fs_find_dir(path) < 0) { found = 1; break; }
    }
    if (!found || fs_write_path(path, png, len) < 0) goto done;
    if (out_path && cap > 0) kstrlcpy(out_path, path, (size_t)cap);
    rc = 0;
done:
    kfree(png);
    __sync_lock_release(&g_capture_busy);
    return rc;
}
