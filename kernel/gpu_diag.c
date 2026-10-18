#include "gpu.h"
#include "gpu_surface.h"
#include "fb.h"
#include "kheap.h"
#include "kstring.h"
#include "terminal.h"
#include "timer.h"
#include "task.h"

void gpu_diagnostics(void) {
    gpu_stats_t s; gpu_stats(&s);
    char line[180];
    terminal_writeln("    3D: CPU rendering (native GL / optional Mesa OSMesa); no hardware 3D backend");
    ksnprintf(line, sizeof(line), "    presentation: %llu copies, %llu KiB, %llu us CPU copy time",
              s.frames, s.copied_bytes >> 10, s.copy_ns / 1000);
    terminal_writeln(line);
    ksnprintf(line, sizeof(line), "    driver: %llu updates, %llu KiB, %llu us; %llu commands, %llu timeouts",
              s.flushes, s.flushed_bytes >> 10, s.flush_ns / 1000, s.commands, s.timeouts);
    terminal_writeln(line);
    ksnprintf(line, sizeof(line), "    damage: %llu frames, %llu KiB changed / %llu KiB considered",
              s.damage_frames, s.damage_bytes >> 10, s.candidate_bytes >> 10);
    terminal_writeln(line);
}

void gpu_tests(void (*report)(const char*, int, const char*)) {
    gpu_surface_tests(report);
    gpu_driver_tests(report);
    const fb_info_t* f = fb_info();
    if (fb_available()) {
        report("live framebuffer dimensions and stride are valid", f->addr && f->width && f->height && f->bpp == 32 &&
               (uint64_t)f->pitch >= (uint64_t)f->width * 4, "");
    } else terminal_writeln("  skip  live framebuffer - text display");
    gpu_stats_t a, b; gpu_stats(&a); gpu_stats(&b);
    report("GPU diagnostic counters never go backwards", b.frames >= a.frames && b.copied_bytes >= a.copied_bytes &&
           b.flushes >= a.flushes && b.timeouts >= a.timeouts, "");
}

void gpu_bench(void) {
    const unsigned pixels = 1024 * 768, bytes = pixels * 4;
    uint32_t* src = kmalloc(bytes), *dst = kmalloc(bytes);
    if (!src || !dst) { kfree(src); kfree(dst); terminal_writeln("gpubench: not enough RAM for two 3 MiB buffers"); return; }
    for (unsigned i = 0; i < pixels; i++) src[i] = i ^ 0x532a71;
    gpu_surface_t a = {src, 1024, 768, 1024}, b = {dst, 1024, 768, 1024};
    uint64_t ns = 0;
    for (int i = 0; i < 64; i++) {
        uint64_t start = timer_counter();
        gpu_surface_copy(b, a, (gpu_rect_t){0, 0, 1024, 768});
        ns += timer_counter() - start;
        if (!(i & 7)) task_yield();
    }
    int good = !memcmp(src, dst, bytes);
    kfree(src); kfree(dst);
    char line[160];
    ksnprintf(line, sizeof(line), "gpubench: offscreen CPU copy, 192 MiB in %llu us (%llu MiB/s), pixels %s",
              ns / 1000, ns ? 192ull * 1000000000 / ns : 0, good ? "correct" : "WRONG");
    terminal_writeln(line);
    terminal_writeln("RAM copies only; excludes scanout, host display latency and 3D rendering.");
    gpu_diagnostics();
}
