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

uint64_t gpu_surface_damage(gpu_surface_t src, gpu_surface_t previous, int valid,
                           void (*changed)(gpu_rect_t, void*), void* context) {
    if (!src.pixels || !previous.pixels || !changed || src.width <= 0 || src.height <= 0 ||
        src.stride < src.width || previous.width < src.width || previous.height < src.height ||
        previous.stride < previous.width) return 0;
    uint64_t bytes = 0;
    for (int y = 0; y < src.height; y += 32) {
        int h = src.height - y < 32 ? src.height - y : 32, run = -1;
        for (int x = 0; x <= src.width; x += 32) {
            int w = src.width - x < 32 ? src.width - x : 32;
            int dirty = x < src.width && !valid;
            if (valid && x < src.width) for (int yy = y; yy < y + h; yy++)
                if (memcmp(src.pixels + (size_t)yy * src.stride + x,
                           previous.pixels + (size_t)yy * previous.stride + x, (size_t)w * 4)) { dirty = 1; break; }
            if (dirty && run < 0) run = x;
            if (!dirty && run >= 0) {
                gpu_rect_t r = {run, y, x - run, h};
                bytes += gpu_surface_copy(previous, src, r); changed(r, context); run = -1;
            }
            /* The final partial tile has no x == width iteration. */
            if (x < src.width && x + 32 >= src.width) {
                if (run >= 0) {
                    gpu_rect_t r = {run, y, src.width - run, h};
                    bytes += gpu_surface_copy(previous, src, r); changed(r, context);
                }
                break;
            }
        }
    }
    return bytes;
}

typedef struct { int n; uint64_t area; } damage_check_t;
static void count_damage(gpu_rect_t r, void* p) {
    damage_check_t* c = p; c->n++; c->area += (uint64_t)r.w * r.h;
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
    uint32_t current[65 * 33], previous[65 * 33];
    memset(current, 0x3a, sizeof(current)); memset(previous, 0, sizeof(previous));
    src = (gpu_surface_t){current, 65, 33, 65}; dst = (gpu_surface_t){previous, 65, 33, 65};
    damage_check_t c = {0, 0};
    n = gpu_surface_damage(src, dst, 0, count_damage, &c);
    report("first damaged frame includes partial edge tiles", n == sizeof(current) && c.area == 65 * 33 && !memcmp(current, previous, sizeof(current)), "");
    c = (damage_check_t){0, 0};
    n = gpu_surface_damage(src, dst, 1, count_damage, &c);
    report("unchanged frames copy no pixels", !n && !c.n, "");
    current[32 * 65 + 64]++;
    n = gpu_surface_damage(src, dst, 1, count_damage, &c);
    report("one edge pixel updates only its damaged tile", n == 4 && c.n == 1 && !memcmp(current, previous, sizeof(current)), "");
    current[0]++; current[64]++;
    c = (damage_check_t){0, 0};
    n = gpu_surface_damage(src, dst, 1, count_damage, &c);
    report("separate damaged tiles leave the clean middle untouched", n == (32 * 32 + 32) * 4 && c.n == 2 && !memcmp(current, previous, sizeof(current)), "");
}
