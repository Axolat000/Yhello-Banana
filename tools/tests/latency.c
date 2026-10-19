/* Host test of the real telemetry module with a deterministic boot-core clock. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../../web/test/shim/types.h"
#define SMP_H
#define LATENCY_HOST_TEST
static int core;
static uint64_t now = 1000000, rate = 1000000;
static uint32_t wall;
static unsigned reports;
static int cpu_id(void) { return core; }
static int cpu_count(void) { return 2; }
static uint64_t cycles(void) { return now; }
static uintptr_t pause_irqs(void) { return 0x200; }
static void resume_irqs(uintptr_t f) { (void)f; }
#include "../../kernel/latency.c"
uint64_t timer_tsc_per_ms(void) { return rate; }
uint32_t timer_ms(void) { return wall; }
uint64_t task_core_ms(int c) { return c ? 25 : 50; }
uint32_t task_core_pct(int c) { return c ? 25 : 50; }
void meminfo_get(meminfo_t* m) { memset(m, 0, sizeof(*m)); }
void zswap_stats(zswap_stats_t* z) { memset(z, 0, sizeof(*z)); }
void klog(const char* fmt, ...) { (void)fmt; reports++; }

int main(void) {
    latency_hist_t h = {0};
    assert(latency_hist_p95(&h) == 0);
    for (unsigned i = 0; i < 95; i++) latency_hist_add(&h, 49000);
    for (unsigned i = 0; i < 5; i++) latency_hist_add(&h, 90000);
    assert(latency_hist_p95(&h) == 50000 && h.max_us == 90000);
    latency_hist_add(&h, 1000001);
    assert(latency_hist_p95(&h) == 100000);
    memset(&h, 0, sizeof(h)); h.count = h.bins[0] = LATENCY_MAX;
    latency_hist_add(&h, LATENCY_MAX);
    assert(h.count == LATENCY_MAX && h.bins[0] == LATENCY_MAX);
    assert(h.max_us == LATENCY_MAX && latency_hist_p95(&h) == 100);
    assert(latency_elapsed_us(0, 1234567, 1000000) == 1234);
    assert(latency_elapsed_us(10, 9, rate) == 0);
    assert(latency_elapsed_us(0, 100, 0) == 0);
    assert(latency_elapsed_us(0, ~(uint64_t)0, rate) == LATENCY_MAX);

    assert(latency_stamp() == 0);
    latency_irq_begin(0x200); assert(g.irq_start == 0);
    latency_set(1);
    uint64_t stamp = latency_stamp();
    now += 51000000; latency_input(stamp);
    assert(g.input.count == 1 && g.input.max_us == 51000);
    latency_irq_begin(0x200);
    uint64_t start = g.irq_start;
    now += 1000000;
    latency_irq_begin(0); latency_irq_end(0);
    assert(g.irq_start == start && !g.irq.count);
    core = 1; latency_irq_begin(0x200); latency_irq_end(0x200);
    latency_input(stamp); latency_drop(); latency_input_poll(); latency_timer();
    assert(g.irq_start == start && !g.irq.count && !g.drops && !g.poll.count);
    core = 0; now += 1000000; latency_irq_end(0x200);
    assert(g.irq.count == 1 && g.irq.max_us == 2000 && !g.irq_start);
    latency_irq_end(0x200); assert(g.irq.count == 1);
    latency_drop(); assert(g.drops == 1);
    g.drops = LATENCY_MAX; latency_drop(); assert(g.drops == LATENCY_MAX);
    latency_input_poll(); latency_timer();
    now += 3000000; latency_input_poll(); latency_timer();
    assert(g.poll.max_us == 3000 && g.timer.max_us == 3000);
    stamp = latency_stamp(); now += 2000000; latency_service(stamp);
    assert(g.service.count == 1 && g.service.max_us == 2000);

    stamp = latency_stamp(); now++; latency_set(1); latency_input(stamp);
    assert(!g.input.count && !g.irq.count && !g.poll.count && !g.drops);
    latency_input(now + 1); assert(!g.input.count);
    reports = 0; wall = 999; latency_poll(); assert(!reports);
    wall = 1000; latency_poll(); assert(reports);
    unsigned saved = reports; latency_poll(); assert(reports == saved);
    wall = 0xfffffff0u; latency_set(1);
    reports = 0; wall += 1000; latency_poll(); assert(reports); /* ms wrap */
    latency_set(0); saved = reports;
    wall += 2000; latency_poll(); latency_input(now); latency_drop();
    assert(reports == saved && !g.input.count && !g.drops);
    rate = 0; latency_set(1); assert(latency_stamp() == 0);
    latency_input_poll(); latency_timer(); assert(!g.poll.count && !g.timer.count);
    puts("latency: host tests passed");
}
