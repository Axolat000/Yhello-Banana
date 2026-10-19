/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crash.h"
#include "kstring.h"

typedef struct {
    char* text;
    size_t size, used;
    int rows;
} output_t;
static void collect(void* user, const char* text, int heading) {
    output_t* o = user;
    (void)heading;
    size_t n = strlen(text);
    if (o->used + n + 2 <= o->size) {
        memcpy(o->text + o->used, text, n);
        o->used += n;
        o->text[o->used++] = '\n';
        o->text[o->used] = 0;
    }
    o->rows++;
}
static void sample(crash_report_t* r, int bits, int kind) {
    crash_sample(r);
    r->bits = bits;
    r->ip = (uintptr_t)0x123456789ABCDEF0ull;
    r->cr2 = (uintptr_t)0xFEDCBA9876543028ull;
    if (kind == 1) {
        r->vector = 13;
        r->error = 0x2B;
    }
    if (kind == 2) {
        r->exception = 0;
        kstrlcpy(r->reason, "Kernel check failed: damaged stack guard", sizeof(r->reason));
    }
    if (kind == 3)
        r->reason[0] = 0;
    if (kind == 4) {
        for (int i = 0; i < 159; i++)
            r->reason[i] = 'A' + i % 26;
        r->reason[159] = 0;
    }
    if (kind == 5)
        r->stack_count = 0;
    if (kind == 6)
        r->error = 1 | 2 | 4 | 8 | 16 | 32 | 64;
    if (kind == 7)
        r->preview = 0;
}
int crash_test_format(char* text, size_t size, int bits, int kind) {
    crash_report_t r;
    sample(&r, bits, kind);
    output_t o = {text, size, 0, 0};
    if (size)
        text[0] = 0;
    crash_format(&r, collect, &o);
    return o.rows;
}
void crash_test_render(uint32_t* pixels, int w, int h, int pitch, int bits, int kind) {
    crash_report_t r;
    sample(&r, bits, kind);
    crash_surface_t s = {pixels, (uint32_t)w, (uint32_t)h, (uint32_t)pitch};
    crash_render(&r, &s);
}
