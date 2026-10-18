#include "boottime.h"
#include "timer.h"
#include "serial.h"

#define MAX_STEPS 64

typedef struct {
    const char* what;
    uint64_t    tsc;
    uint64_t    ns;                     /* the clock's, if it ran then */
    int         clocked;
} mark_t;

static mark_t g_marks[MAX_STEPS];
static int    g_n;
static int    g_clock;                  /* timer_counter() can be read */

static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void boottime_clock_started(void) { g_clock = 1; }

void boottime_mark(const char* what) {
    if (g_n < MAX_STEPS) {
        mark_t* m = &g_marks[g_n++];
        m->what = what;
        m->tsc = rdtsc();
        m->clocked = g_clock;
        m->ns = g_clock ? timer_counter() : 0;
    }
    if (g_clock) {
        const mark_t* c0 = NULL;                /* (the first one the clock timed) */
        for (int i = 0; i < g_n && !c0; i++) if (g_marks[i].clocked) c0 = &g_marks[i];
        uint64_t ms = c0 ? (timer_counter() - c0->ns) / 1000000u : 0;
        klog("boot: %s (%u ms after the timer)\n", what, (uint32_t)ms);
    } else {
        klog("boot: %s\n", what);
    }
}

/* TSC cycles per nanosecond x 2^20, from the clocked marks furthest apart
 * (0: not known yet) */
static uint64_t tsc_rate(const mark_t** first) {
    const mark_t *a = NULL, *b = NULL;
    for (int i = 0; i < g_n; i++) {
        if (!g_marks[i].clocked) continue;
        if (!a) a = &g_marks[i];
        b = &g_marks[i];
    }
    *first = a;
    if (!a || b == a || b->ns - a->ns < 20000000u || b->tsc <= a->tsc) return 0;
    return ((b->tsc - a->tsc) << 20) / (b->ns - a->ns);
}

int boottime_steps(boot_step_t* out, int max) {
    const mark_t* c0;
    uint64_t rate = tsc_rate(&c0);
    if (!g_n) return 0;
    /* the first clocked mark's time since the kernel's: by the TSC */
    uint64_t c0_at = c0 && rate && c0->tsc > g_marks[0].tsc ? ((c0->tsc - g_marks[0].tsc) << 20) / rate : 0;
    int n = 0;
    for (int i = 0; i < g_n && n < max; i++) {
        const mark_t* m = &g_marks[i];
        uint64_t ns;
        if (m->clocked && c0) ns = c0_at + (m->ns - c0->ns);
        else if (rate && m->tsc >= g_marks[0].tsc) ns = ((m->tsc - g_marks[0].tsc) << 20) / rate;
        else ns = 0;                            /* (before the timer, and no rate yet) */
        if (n && ns < out[n - 1].ns) ns = out[n - 1].ns;   /* (a TSC that went back: never before the last) */
        out[n].what = m->what;
        out[n].ns = ns;
        n++;
    }
    return n;
}

uint64_t boottime_before_ns(void) {
    const mark_t* c0;
    uint64_t rate = tsc_rate(&c0);
    /* (the TSC counts from the processor's reset: the firmware, the loader -
     * unless it was not reset: a warm restart, some hypervisors) */
    if (!g_n || !rate || g_marks[0].tsc >= (1ull << 43)) return 0;
    uint64_t ns = (g_marks[0].tsc << 20) / rate;
    return ns < 600ull * 1000000000ull ? ns : 0;          /* (over 10 minutes: not since a reset) */
}
