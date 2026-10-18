#include "gpu_surface.h"
#include "kstring.h"

int gpu_rect_clip(gpu_rect_t* r, int width, int height) {
    if (r->w <= 0 || r->h <= 0 || width <= 0 || height <= 0) return 0;
    /* Widen before adding: even INT_MIN / INT_MAX inputs are defined. */
    int64_t right = (int64_t)r->x + r->w, bottom = (int64_t)r->y + r->h;
    int x = r->x < 0 ? 0 : r->x, y = r->y < 0 ? 0 : r->y;
    if (right > width) right = width;
    if (bottom > height) bottom = height;
    if (right <= x || bottom <= y) return 0;
    *r = (gpu_rect_t){x, y, (int)right - x, (int)bottom - y};
    return 1;
}

uint64_t gpu_surface_copy(gpu_surface_t dst, gpu_surface_t src, gpu_rect_t r) {
    if (!dst.pixels || !src.pixels || dst.stride < dst.width || src.stride < src.width) return 0;
    int w = dst.width < src.width ? dst.width : src.width;
    int h = dst.height < src.height ? dst.height : src.height;
    if (!gpu_rect_clip(&r, w, h)) return 0;
    for (int y = r.y; y < r.y + r.h; y++)
        memcpy(dst.pixels + (size_t)y * dst.stride + r.x,
               src.pixels + (size_t)y * src.stride + r.x, (size_t)r.w * 4);
    return (uint64_t)r.w * r.h * 4;
}

void gpu_surface_tests(void (*report)(const char*, int, const char*)) {
    uint32_t a[10 * 7 + 2], b[12 * 7 + 2];
    for (int i = 0; i < 72; i++) a[i] = 0x11223344;
    for (int i = 0; i < 86; i++) b[i] = 0xdeadbeef;
    gpu_surface_t src = {a + 1, 8, 7, 10}, dst = {b + 1, 8, 7, 12};
    for (int y = 0; y < 7; y++) for (int x = 0; x < 8; x++) src.pixels[y * 10 + x] = y * 8 + x;
    uint64_t n = gpu_surface_copy(dst, src, (gpu_rect_t){-2, -3, 20, 20});
    int ok = n == 8 * 7 * 4;
    for (int y = 0; y < 7; y++) for (int x = 0; x < 12; x++)
        if (dst.pixels[y * 12 + x] != (x < 8 ? (uint32_t)(y * 8 + x) : 0xdeadbeef)) ok = 0;
    report("clipped framebuffer copy preserves row padding and pixels", ok, "offscreen surfaces");
    report("framebuffer guard words stay intact", a[0] == 0x11223344 && a[71] == 0x11223344 && b[0] == 0xdeadbeef && b[85] == 0xdeadbeef, "");
    src.pixels[3 * 10 + 2] = 0xabcdef;
    n = gpu_surface_copy(dst, src, (gpu_rect_t){2, 3, 1, 1});
    report("single-pixel presentation copies exactly four bytes", n == 4 && dst.pixels[3 * 12 + 2] == 0xabcdef && dst.pixels[3 * 12 + 3] == 27, "");
    gpu_rect_t r = {0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff};
    ok = !gpu_rect_clip(&r, 8, 7);
    r = (gpu_rect_t){(-2147483647 - 1), 0, 0x7fffffff, 2};
    ok &= !gpu_rect_clip(&r, 8, 7);
    ok &= !gpu_surface_copy(dst, src, (gpu_rect_t){0, 0, -1, 2});
    ok &= !gpu_surface_copy(dst, src, (gpu_rect_t){8, 0, 1, 1});
    report("empty, offscreen and overflowing rectangles are rejected", ok, "");
    src.width = 4; src.height = 2;
    report("presentation respects both surface sizes", gpu_surface_copy(dst, src, (gpu_rect_t){0, 0, 8, 7}) == 32, "");
}
