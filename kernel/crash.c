/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crash.h"
#include "fb.h"
#include "gpu.h"
#include "kstring.h"
#include "serial.h"
#include "smp.h"
#include "sysinfo.h"
#include "task.h"
#include "timer.h"

static volatile int g_owner = -1;
static volatile int g_preview;
int crash_in_progress(void) {
    return g_owner >= 0;
}
int crash_preview_active(void) {
    return g_preview;
}
void crash_set_preview(int on) {
    g_preview = on;
}

void crash_stop_secondary(void) {
    if (g_owner >= 0 && g_owner != cpu_id())
        for (;;)
            __asm__ volatile("cli; hlt");
}

void crash_capture_context(crash_report_t* r) {
    uintptr_t bottom, top;
    r->bits = sizeof(uintptr_t) * 8;
    r->cpu = (uint32_t)cpu_id();
    r->cpus = (uint32_t)cpu_count();
    r->pid = task_crash_context(r->task, &bottom, &top);
    if (!r->task[0])
        kstrlcpy(r->task, "early boot / unknown", sizeof(r->task));
    r->uptime_ms = timer_ms();
    const sysinfo_t* si = sysinfo_get();
    r->uefi = si ? si->uefi : 0;
    __asm__ volatile("mov %%cr0,%0" : "=r"(r->cr0));
    __asm__ volatile("mov %%cr2,%0" : "=r"(r->cr2));
    __asm__ volatile("mov %%cr3,%0" : "=r"(r->cr3));
    __asm__ volatile("mov %%cr4,%0" : "=r"(r->cr4));
    r->stack_count = 0;
    /* Do not chase arbitrary pointers or an overflowing stack. Only read
     * words inside this task's known, allocated kernel stack. A nested
     * fault falls back to the emergency serial/VGA message below. */
    uintptr_t sp = r->sp;
    if (bottom && top > bottom && sp >= bottom && sp <= top && !(sp & (sizeof(uintptr_t) - 1))) {
        size_t n = (top - sp) / sizeof(uintptr_t);
        if (n > CRASH_STACK)
            n = CRASH_STACK;
        for (size_t i = 0; i < n; i++)
            r->stack[r->stack_count++] = ((volatile uintptr_t*)sp)[i];
    }
}

static void serial_line(void* unused, const char* text, int heading) {
    void (*write)(const char*) = unused ? serial_write : serial_panic_write;
    if (heading)
        write("\n");
    write(text);
    write("\n");
}

typedef struct {
    int row;
} vga_t;
static void vga_line(void* user, const char* text, int heading) {
    vga_t* v = user;
    volatile uint16_t* cells = (volatile uint16_t*)0xB8000;
    if (v->row >= 23)
        return;
    for (int i = 0; text[i] && i < 78; i++)
        cells[v->row * 80 + i + 1] = (uint8_t)text[i] | (heading ? 0x1E00 : 0x1F00);
    v->row++;
}

void crash_show(const crash_report_t* r, int serial) {
    if (fb_available()) {
        const fb_info_t* fi = fb_info();
        if (fi->addr && fi->bpp == 32 && fi->width <= 16384 && fi->height <= 16384 &&
            fi->pitch >= fi->width * 4 && !(fi->pitch & 3) &&
            (uint64_t)fi->pitch * fi->height <= (uintptr_t)-1 - fi->addr) {
            crash_surface_t s = {(volatile uint32_t*)fi->addr, fi->width, fi->height, fi->pitch};
            crash_render(r, &s);
            __sync_synchronize();
            if (r->preview)
                gpu_flush(0, 0, (int)fi->width, (int)fi->height);
            else
                gpu_panic_present();
        }
    } else {
        volatile uint16_t* cells = (volatile uint16_t*)0xB8000;
        for (int i = 0; i < 80 * 25; i++)
            cells[i] = ' ' | 0x1F00;
        vga_t v = {0};
        vga_line(&v,
                 r->preview ? "BANANA OS - CRASH SCREEN PREVIEW" : "BANANA OS STOPPED - fatal kernel error",
                 1);
        crash_format(r, vga_line, &v);
        v.row = 23; /* footer always stays visible */
        for (int i = 0; i < 78; i++)
            cells[24 * 80 + i + 1] = ' ' | 0x1F00;
        const char* help = r->preview ? "Enter / Esc: return to the OS"
                                      : "Restart with the power button / VM controls. Full report on COM1.";
        for (int i = 0; help[i] && i < 78; i++)
            cells[24 * 80 + i + 1] = (uint8_t)help[i] | 0x1F00;
    }
    __sync_synchronize(); /* make write-combined framebuffer stores visible before halting */
    if (serial) {
        if (!r->preview)
            serial_drain();
        if (r->preview) {
            serial_write("\n--- Simulated crash report (OS still running) ---\n");
            crash_format(r, serial_line, (void*)1);
            serial_write("--- End simulated report ---\n");
        } else {
            serial_panic_write("\n*** PANIC: Banana OS fatal kernel error ***\n");
            crash_format(r, serial_line, NULL);
            serial_panic_write("System halted. Restart manually; no crash dump was written to disk.\n");
        }
    }
}

void crash_abort_nested(void) {
    static volatile int entered;
    __asm__ volatile("cli" ::: "memory");
    if (!__sync_lock_test_and_set(&entered, 1)) {
        serial_panic_write("\nPANIC: nested fault while collecting diagnostics; halting.\n");
        volatile uint16_t* cells = (volatile uint16_t*)0xB8000;
        const char* msg = "Nested kernel fault: diagnostics incomplete. See serial / restart.";
        for (int i = 0; msg[i] && i < 80; i++)
            cells[i] = (uint8_t)msg[i] | 0x4F00;
    }
    for (;;)
        __asm__ volatile("cli; hlt");
}

void crash_fatal(crash_report_t* r) {
    __asm__ volatile("cli" ::: "memory");
    int cpu = cpu_id();
    int previous = __sync_val_compare_and_swap(&g_owner, -1, cpu);
    if (previous >= 0) {
        if (previous == cpu)
            crash_abort_nested();
        for (;;)
            __asm__ volatile("cli; hlt");
    }
    smp_panic_stop_others();
    serial_panic_write("\n*** PANIC: collecting kernel diagnostics ***\n");
    crash_capture_context(r);
    crash_show(r, 1);
    for (;;)
        __asm__ volatile("cli; hlt");
}
