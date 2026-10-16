#ifndef CRASH_H
#define CRASH_H
#include "types.h"

/* A fixed-size snapshot: no allocator, file system or desktop in the panic path. */
#define CRASH_REGS 16
#define CRASH_STACK 8
typedef struct {
    uint32_t vector, error, cpu, cpus, uptime_ms;
    int pid, preview, exception, uefi, bits, stack_count;
    char task[24], reason[160];
    uintptr_t ip, sp, bp, flags, cs, ss, cr0, cr2, cr3, cr4;
    uintptr_t reg[CRASH_REGS], stack[CRASH_STACK];
} crash_report_t;
typedef struct {
    volatile uint32_t* pixels;
    uint32_t width, height, pitch; /* pitch in bytes; XRGB8888 */
} crash_surface_t;
typedef void (*crash_emit_t)(void* user, const char* line, int heading);

/* Pure formatting/rendering, also used by host tests and the safe preview. */
const char* crash_stop_code(const crash_report_t* report);
void crash_format(const crash_report_t* report, crash_emit_t emit, void* user);
void crash_render(const crash_report_t* report, const crash_surface_t* surface);
void crash_sample(crash_report_t* report);

/* Privileged capture/output. Only an unrecoverable kernel fault calls fatal. */
void crash_capture_context(crash_report_t* report);
void crash_fatal(crash_report_t* report) __attribute__((noreturn));
int crash_in_progress(void);
void crash_stop_secondary(void);
void crash_abort_nested(void) __attribute__((noreturn));
int crash_preview_active(void);
void crash_set_preview(int on);
void crash_show(const crash_report_t* report, int serial);
#endif
