#include "crashtest.h"
#include "../kernel/crash.h"
#include "../kernel/fb.h"
#include "../kernel/gpu.h"
#include "../kernel/gui.h"
#include "../kernel/idt.h"
#include "../kernel/keyboard.h"
#include "../kernel/kstring.h"
#include "../kernel/task.h"
#include "../kernel/terminal.h"
#include "../kernel/timer.h"
#include "../kernel/tty.h"

static void print_line(void* unused, const char* text, int heading) {
    (void)unused;
    if (heading)
        terminal_putchar('\n');
    terminal_writeln(text);
}

int crashtest_cmd(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "preview";
    if (argc > 2 || (strcmp(mode, "preview") && strcmp(mode, "report") && strcmp(mode, "panic") &&
                     strcmp(mode, "ud2") && strcmp(mode, "pagefault"))) {
        terminal_writeln("usage: crashtest [preview | report | panic | ud2 | pagefault]");
        terminal_writeln("preview/report are safe. panic/ud2/pagefault HALT the OS; save work first.");
        return 1;
    }
    if (!strcmp(mode, "panic"))
        kernel_panic("Manual crash-screen test requested with crashtest panic");
    if (!strcmp(mode, "ud2"))
        __asm__ volatile("ud2");
    if (!strcmp(mode, "pagefault")) {
#ifdef __x86_64__
        uintptr_t address = 0; /* page zero is deliberately unmapped */
        __asm__ volatile("mov (%0),%%rax" ::"r"(address) : "rax", "memory");
        terminal_writeln("crashtest: page zero is mapped; no fault was triggered.");
#else
        terminal_writeln("crashtest: page-fault testing needs the 64-bit kernel (32-bit has no paging).");
#endif
        return 1;
    }
    crash_report_t r;
    crash_sample(&r);
    /* The example frame is fictional; the CPU/task/uptime context is live. */
    crash_capture_context(&r);
    r.cr2 = 0x28;
    r.stack_count = CRASH_STACK;
    for (int i = 0; i < CRASH_STACK; i++)
        r.stack[i] = 0x00101234u + i * 0x120;
    if (!strcmp(mode, "report")) {
        terminal_writeln("Simulated crash report - the OS keeps running.");
        crash_format(&r, print_line, NULL);
        return 1;
    }
    if (tty_current() >= 0) {
        terminal_writeln("crashtest: preview uses the local display; over SSH use crashtest report.");
        return 1;
    }
    if (crash_preview_active()) {
        terminal_writeln("crashtest: another preview is already open.");
        return 1;
    }
    terminal_writeln("Crash-screen preview: Enter or Esc returns to the OS.");
    terminal_flush();
    /* Do not change desktop mode or the terminal grid. Other tasks keep
     * running; their normal presentation waits until the preview ends. */
    keyboard_set_owner(task_current_pid());
    static uint16_t vga_saved[80 * 25];
    int vga = !fb_available();
    if (vga)
        for (int i = 0; i < 80 * 25; i++)
            vga_saved[i] = ((volatile uint16_t*)0xB8000)[i];
    terminal_mode_t previous = terminal_get_mode();
    terminal_set_mode(TERMINAL_MODE_SUSPENDED);
    crash_set_preview(1);
    if (gpu_has_hw_cursor())
        gpu_cursor_move(0, 0, 0);
    crash_show(&r, 1);
    for (;;) {
        char c = keyboard_try_getchar();
        if (c == '\n' || c == '\r' || c == 27)
            break;
        task_sleep_ms(10);
    }
    crash_set_preview(0);
    keyboard_set_owner(-1);
    if (vga)
        for (int i = 0; i < 80 * 25; i++)
            ((volatile uint16_t*)0xB8000)[i] = vga_saved[i];
    terminal_set_mode(previous);
    terminal_screen_changed();
    if (gui_is_enabled())
        gui_screen_changed();
    terminal_flush();
    terminal_writeln("Crash-screen preview closed. The OS is still running.");
    return 1;
}
