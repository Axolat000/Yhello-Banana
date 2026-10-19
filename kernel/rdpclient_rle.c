/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "types.h"

/*
 * RDP bitmap decompressors for the client (kernel/rdpclient.c): the RDP 6.0
 * planar codec (MS-RDPEGDI 2.2.2.5.1, 32 bpp) and the interleaved RLE
 * (MS-RDPBCGR 2.2.9.1.1.3.1.2.4, 15/16/24 bpp). Each writes a top-down
 * ARGB image (0x00RRGGBB) of w*h pixels into `out`. Returns 0, or -1 on
 * malformed input.
 */

/* ── RDP 6.0 planar (32 bpp) ──────────────────────────────────────── */

#define PLANAR_NA  0x20     /* no alpha plane */
#define PLANAR_RLE 0x10     /* planes are RLE-compressed */
#define PLANAR_CS  0x08     /* chroma subsampling (not supported) */

/* one RLE plane: w*h bytes into `out`. Returns the bytes consumed, or -1.
 * Each row is a run of segments; a segment copies cRawBytes literal bytes
 * then repeats the last value nRunLength times (MS-RDPEGDI 3.1.9.2). */
static int plane_rle(const uint8_t* in, uint32_t len, uint8_t* out, int w, int h) {
    uint32_t i = 0;
    uint8_t last = 0;
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < w) {
            if (i >= len) return -1;
            uint8_t cb = in[i++];
            uint32_t run = cb & 0x0F;
            uint32_t raw = (cb >> 4) & 0x0F;
            if (run == 1) { run = raw + 16; raw = 0; }
            else if (run == 2) { run = raw + 32; raw = 0; }
            for (uint32_t k = 0; k < raw && x < w; k++) {
                if (i >= len) return -1;
                last = in[i++];
                out[y * w + x++] = last;
            }
            for (uint32_t k = 0; k < run && x < w; k++) out[y * w + x++] = last;
        }
    }
    return (int)i;
}

extern void* kmalloc(unsigned long);
extern void  kfree(void*);

int rdp_planar_decompress(const uint8_t* in, uint32_t in_len, int w, int h, uint32_t* out) {
    if (in_len < 1) return -1;
    uint8_t hdr = in[0];
    if ((hdr & 0x07) || (hdr & PLANAR_CS)) return -1;   /* color loss / subsampling: unsupported */
    uint32_t sz = (uint32_t)w * h;
    uint8_t* R = (uint8_t*)kmalloc(sz);
    uint8_t* G = (uint8_t*)kmalloc(sz);
    uint8_t* B = (uint8_t*)kmalloc(sz);
    uint8_t* A = (uint8_t*)kmalloc(sz);
    int ok = R && G && B && A;
    uint32_t off = 1;
    int planes = (hdr & PLANAR_NA) ? 3 : 4;
    if (ok && !(hdr & PLANAR_RLE)) {                    /* raw planes: [A] R G B, each sz bytes */
        if (1 + (uint32_t)planes * sz > in_len) ok = 0;
        else {
            uint32_t p = 1;
            if (!(hdr & PLANAR_NA)) { __builtin_memcpy(A, in + p, sz); p += sz; }
            __builtin_memcpy(R, in + p, sz); p += sz;
            __builtin_memcpy(G, in + p, sz); p += sz;
            __builtin_memcpy(B, in + p, sz);
        }
    } else if (ok) {                                    /* RLE planes */
        int r;
        if (!(hdr & PLANAR_NA)) { r = plane_rle(in + off, in_len - off, A, w, h); if (r < 0) ok = 0; else off += (uint32_t)r; }
        if (ok) { r = plane_rle(in + off, in_len - off, R, w, h); if (r < 0) ok = 0; else off += (uint32_t)r; }
        if (ok) { r = plane_rle(in + off, in_len - off, G, w, h); if (r < 0) ok = 0; else off += (uint32_t)r; }
        if (ok) { r = plane_rle(in + off, in_len - off, B, w, h); if (r < 0) ok = 0; else off += (uint32_t)r; }
    }
    if (ok)
        for (uint32_t k = 0; k < sz; k++)
            out[k] = (uint32_t)R[k] << 16 | (uint32_t)G[k] << 8 | B[k];
    kfree(R); kfree(G); kfree(B); kfree(A);
    return ok ? 0 : -1;
}

/* ── interleaved RLE (15/16/24 bpp) ───────────────────────────────── */

/* pixel size in bytes for the interleaved codec */
static int bpp_bytes(int bpp) { return bpp == 24 ? 3 : bpp == 8 ? 1 : 2; }

static uint32_t rd_pixel(const uint8_t* p, int bytes) {
    if (bytes == 1) return p[0];
    if (bytes == 2) return (uint32_t)p[0] | p[1] << 8;
    return (uint32_t)p[0] | p[1] << 8 | (uint32_t)p[2] << 16;
}

/*
 * Interleaved RLE (MS-RDPBCGR 2.2.9.1.1.3.1.2.4), the classic RDP bitmap
 * compression. Decodes into `raw`, w*h native pixels (bpp_bytes each).
 * The order forms are REGULAR (3-bit code, 5-bit length), LITE (4-bit
 * code, 4-bit length) and MEGA_MEGA (length in the next two bytes); the
 * orders are Fill, Mix, FillOrMix, Colour run, Colour image, SetFg run,
 * SetFg FOM, Dithered (bicolour), the two special FOM orders and the
 * white/black single pixels. Adapted from the public algorithm in
 * rdesktop's bitmap.c.
 */
static int interleaved(const uint8_t* in, uint32_t len, uint8_t* raw, int w, int h, int bytes) {
    uint32_t ip = 0, op = 0;
    uint32_t total = (uint32_t)w * h;
    uint32_t fgpel = (bytes == 3) ? 0xFFFFFFu : (bytes == 2) ? 0xFFFFu : 0xFFu;
    uint32_t mix = fgpel;
    int lastop = -1, fom_insert = 0;

    #define WR(v) do { if (op >= total) return -1; \
        if (bytes == 1) raw[op] = (uint8_t)(v); \
        else if (bytes == 2) { raw[op*2]=(uint8_t)(v); raw[op*2+1]=(uint8_t)((uint32_t)(v)>>8); } \
        else { raw[op*3]=(uint8_t)(v); raw[op*3+1]=(uint8_t)((uint32_t)(v)>>8); raw[op*3+2]=(uint8_t)((uint32_t)(v)>>16); } \
        op++; } while (0)
    #define ABOVE() (op >= (uint32_t)w ? rd_pixel(raw + (op - (uint32_t)w) * bytes, bytes) : 0u)

    while (ip < len && op < total) {
        uint8_t code = in[ip++];
        uint32_t hi = code >> 4;
        int op_;                                     /* decoded order */
        uint32_t count;
        /* A FGBG / fill-or-mix order (2, or lite 7) counts its length in
         * groups of 8 pixels (one mask byte each); the runs count pixels.
         * A length field of 0 means the real length is in the next byte. */
        if (hi == 0xF) {                             /* MEGA_MEGA / special */
            op_ = code & 0x0F;
            if (op_ < 9) { if (ip + 2 > len) return -1; count = in[ip] | in[ip + 1] << 8; ip += 2; }
            else if (op_ < 0x0B) count = 8;          /* special FGBG */
            else count = 1;                          /* white / black */
        } else if (hi >= 0xC && hi <= 0xE) {         /* LITE */
            op_ = hi - 6;                            /* 6,7,8 */
            uint32_t f = code & 0x0F;
            if (op_ == 7) count = f ? f * 8 : (ip < len ? in[ip++] + 1u : 0);
            else          count = f ? f     : (ip < len ? in[ip++] + 16u : 0);
        } else {                                     /* REGULAR */
            op_ = code >> 5;                         /* 0..5 */
            uint32_t f = code & 0x1F;
            if (op_ == 2) count = f ? f * 8 : (ip < len ? in[ip++] + 1u : 0);
            else          count = f ? f     : (ip < len ? in[ip++] + 32u : 0);
        }

        switch (op_) {
        case 0:                                      /* Fill (background run) */
            if (lastop == 0 && (op % (uint32_t)w) != 0) { WR(ABOVE() ^ mix); }   /* insert a mix pixel */
            for (uint32_t k = 0; k < count; k++) WR(ABOVE());
            break;
        case 1:                                      /* Mix (foreground run) */
            for (uint32_t k = 0; k < count; k++) WR(ABOVE() ^ mix);
            break;
        case 2:                                      /* Fill-or-Mix (FGBG image) */
        fom:
            for (uint32_t k = 0; k < count;) {
                if (ip >= len) return -1;
                uint8_t m = in[ip++];
                for (int b = 0; b < 8 && k < count; b++, k++) WR((m & (1 << b)) ? (ABOVE() ^ mix) : ABOVE());
            }
            break;
        case 3:                                      /* Colour run */
            { if (ip + bytes > len) return -1; uint32_t c = rd_pixel(in + ip, bytes); ip += bytes;
              for (uint32_t k = 0; k < count; k++) WR(c); }
            break;
        case 4:                                      /* Colour image (raw) */
            for (uint32_t k = 0; k < count; k++) { if (ip + bytes > len) return -1; WR(rd_pixel(in + ip, bytes)); ip += bytes; }
            break;
        case 6:                                      /* Set-FG then Mix run */
            if (ip + bytes > len) return -1;
            mix = rd_pixel(in + ip, bytes); ip += bytes;
            for (uint32_t k = 0; k < count; k++) WR(ABOVE() ^ mix);
            break;
        case 7:                                      /* Set-FG then FOM */
            if (ip + bytes > len) return -1;
            mix = rd_pixel(in + ip, bytes); ip += bytes;
            goto fom;
        case 8:                                      /* Dithered / bicolour */
            { if (ip + 2 * bytes > len) return -1; uint32_t c1 = rd_pixel(in + ip, bytes); ip += bytes;
              uint32_t c2 = rd_pixel(in + ip, bytes); ip += bytes;
              for (uint32_t k = 0; k < count; k++) { WR(c1); WR(c2); } }
            break;
        case 9:                                      /* SPECIAL_FGBG_1: FOM mask 0x03, 8 px */
            { uint8_t m = 0x03; for (int b = 0; b < 8; b++) WR((m & (1 << b)) ? (ABOVE() ^ mix) : ABOVE()); }
            break;
        case 0xA:                                    /* SPECIAL_FGBG_2: FOM mask 0x05, 8 px */
            { uint8_t m = 0x05; for (int b = 0; b < 8; b++) WR((m & (1 << b)) ? (ABOVE() ^ mix) : ABOVE()); }
            break;
        case 0xD:                                    /* White */
            WR(fgpel); break;
        case 0xE:                                    /* Black */
            WR(0); break;
        default:
            return -1;
        }
        lastop = op_;
        (void)fom_insert;
    }
    #undef WR
    #undef ABOVE
    return op >= total ? 0 : -1;
}

int rdp_rle_decompress(const uint8_t* in, uint32_t in_len, int bpp, int w, int h, uint32_t* out) {
    int bytes = bpp_bytes(bpp);
    extern void* kmalloc(unsigned long);
    extern void  kfree(void*);
    uint8_t* raw = (uint8_t*)kmalloc((uint32_t)w * h * bytes);
    if (!raw) return -1;
    int ok = interleaved(in, in_len, raw, w, h, bytes) == 0;
    if (ok) {
        for (int i = 0; i < w * h; i++) {
            uint32_t v = rd_pixel(raw + (uint32_t)i * bytes, bytes), r, g, b;
            if (bytes == 2) { r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F;
                              out[i] = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2); }
            else if (bytes == 3) { out[i] = v & 0xFFFFFF; }
            else { out[i] = (uint32_t)v << 16 | v << 8 | v; }
        }
    }
    kfree(raw);
    return ok ? 0 : -1;
}
