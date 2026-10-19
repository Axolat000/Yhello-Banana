/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gpu.h"
#include "gpu_virgl.h"
#include "gpu_surface.h"
#include "fb.h"
#include "kheap.h"
#include "kstring.h"
#include "terminal.h"
#include "timer.h"
#include "task.h"
#include "gui.h"

void gpu_diagnostics(void) {
    gpu_stats_t s; gpu_stats(&s);
    gpu_virgl_diagnostics();
    char line[180];
    terminal_writeln(gpu_virgl_available() ? "    3D: virgl transport available; applications choose their Mesa renderer" : "    3D: CPU rendering (native GL / optional Mesa OSMesa); virgl unavailable");
    ksnprintf(line, sizeof(line), "    presentation: %llu copies, %llu KiB, %llu us CPU copy time",
              s.frames, s.copied_bytes >> 10, s.copy_ns / 1000);
    terminal_writeln(line);
    ksnprintf(line, sizeof(line), "    driver: %llu updates, %llu KiB, %llu us; %llu commands, %llu timeouts",
              s.flushes, s.flushed_bytes >> 10, s.flush_ns / 1000, s.commands, s.timeouts);
    terminal_writeln(line);
    ksnprintf(line, sizeof(line), "    damage: %llu frames, %llu KiB changed / %llu KiB considered",
              s.damage_frames, s.damage_bytes >> 10, s.candidate_bytes >> 10);
    terminal_writeln(line);
    ksnprintf(line, sizeof(line), "    scanout: %llu flips, %llu retrace waits (%llu timed out)", s.flips, s.vblank_waits, s.vblank_timeouts);
    terminal_writeln(line);
}

static void presentation_test(void (*report)(const char*, int, const char*)) {
    if (!fb_available() || gui_is_enabled()) {
        terminal_writeln("  skip  live page flip - run selftest gpu at the console before startx"); return;
    }
    fb_info_t original = *fb_info();
    uint64_t size = (uint64_t)original.width * original.height * 4;
    if (size > 64u * 1024 * 1024) return;
    uint32_t* image = kmalloc((size_t)size);
    if (!image) { terminal_writeln("  skip  live page flip - snapshot allocation failed"); return; }
    gpu_surface_t saved = {image, (int)original.width, (int)original.height, (int)original.width};
    gpu_surface_t screen = {(uint32_t*)original.addr, saved.width, saved.height, (int)(original.pitch / 4)};
    gpu_rect_t full = {0, 0, saved.width, saved.height};
    gpu_surface_copy(saved, screen, full);
    gpu_stats_t a, b; gpu_stats(&a);
    int available = 1, good = 1;
    for (int i = 0; i < 2; i++) {
        gpu_surface_t target;
        if (!gpu_present_begin(&target)) { available = 0; break; }
        gpu_surface_copy(target, saved, full);
        gpu_present_end(saved, &full, 1);
        const fb_info_t* f = fb_info();
        for (unsigned y = 0; y < f->height; y++)
            if (memcmp((void*)(f->addr + (uintptr_t)y * f->pitch), image + (size_t)y * f->width, (size_t)f->width * 4)) good = 0;
    }
    gpu_stats(&b); fb_invalidate_damage(); kfree(image);
    if (available) report("two live page flips preserve pixels and restore the visible page",
        good && fb_info()->addr == original.addr && b.flips - a.flips == 2, "current image retained");
    else terminal_writeln("  skip  live page flip - driver or video memory does not support two pages");
}

void gpu_tests(void (*report)(const char*, int, const char*)) {
    gpu_surface_tests(report);
    gpu_driver_tests(report);
    gpu_virgl_tests(report);
    presentation_test(report);
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
