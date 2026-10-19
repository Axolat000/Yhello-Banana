#include "examples.h"
#include "fs.h"
#include "kheap.h"
#include "types.h"

char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen);

extern const uint8_t ex_hello[], ex_hello_end[], ex_guess[], ex_guess_end[], ex_paint[], ex_paint_end[];
extern const uint8_t ex_clock[], ex_clock_end[], ex_tones[], ex_tones_end[], ex_mandel[], ex_mandel_end[];
extern const uint8_t ex_threads[], ex_threads_end[];
extern const uint8_t ex_webview[], ex_webview_end[];
extern const uint8_t ex_gears[], ex_gears_end[];
extern const uint8_t ex_cltest[], ex_cltest_end[];
extern const uint8_t ex_gl3test[], ex_gl3test_end[];
extern const uint8_t ex_systest[], ex_systest_end[];
extern const uint8_t ex_posixtest[], ex_posixtest_end[];
extern const uint8_t ex_mousetest[], ex_mousetest_end[];

static const struct { const char* name; const uint8_t* start; const uint8_t* end; } g_examples[] = {
    { "hello.bpk", ex_hello, ex_hello_end },
    { "guess.bpk", ex_guess, ex_guess_end },
    { "paint.bpk", ex_paint, ex_paint_end },
    { "clock.bpk", ex_clock, ex_clock_end },
    { "tones.bpk", ex_tones, ex_tones_end },
    { "mandel.bpk", ex_mandel, ex_mandel_end },
    { "threads.bpk", ex_threads, ex_threads_end },
    { "webview.bpk", ex_webview, ex_webview_end },
    { "gears.bpk", ex_gears, ex_gears_end },
    { "cltest.bpk", ex_cltest, ex_cltest_end },
    { "gl3test.bpk", ex_gl3test, ex_gl3test_end },
    { "systest.bpk", ex_systest, ex_systest_end },
    { "posixtest.bpk", ex_posixtest, ex_posixtest_end },
    { "mousetest.bpk", ex_mousetest, ex_mousetest_end },
};

void examples_seed(void) {
    if (fs_mkdir_p("/home/banana/Examples") < 0) return;
    for (unsigned i = 0; i < sizeof(g_examples) / sizeof(g_examples[0]); i++) {
        char path[FS_PATH_LEN];
        int n = 0;
        const char* pre = "/home/banana/Examples/";
        for (const char* s = pre; *s; s++) path[n++] = *s;
        for (const char* s = g_examples[i].name; *s && n < FS_PATH_LEN - 1; s++) path[n++] = *s;
        path[n] = 0;
        /* (the kernel has them compressed: kernel/exbin.asm) */
        int len = 0;
        char* bpk = stbi_zlib_decode_malloc((const char*)g_examples[i].start, (int)(g_examples[i].end - g_examples[i].start), &len);
        if (bpk && len > 0) fs_write_path(path, bpk, (uint32_t)len);
        kfree(bpk);
    }
}

/* the App Store's builtin: source serves them too (kernel/builtin_apps.c);
 * examples_data: the compressed package, as the kernel has it */
int examples_count(void) { return (int)(sizeof(g_examples) / sizeof(g_examples[0])); }
const char* examples_file(int i) { return i >= 0 && i < examples_count() ? g_examples[i].name : ""; }
const uint8_t* examples_data(int i, uint32_t* len) {
    if (i < 0 || i >= examples_count()) return 0;
    *len = (uint32_t)(g_examples[i].end - g_examples[i].start);
    return g_examples[i].start;
}
