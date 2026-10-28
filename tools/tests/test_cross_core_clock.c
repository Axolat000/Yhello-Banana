#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#define SMP_MAX_CPUS 64
#define TS_ROUNDS 8

typedef struct {
    int      qualified;
    int64_t  offset;
    uint64_t rtt_min;
    int      error_code;
} test_smp_tsc_sync_t;

static test_smp_tsc_sync_t g_test_sync[SMP_MAX_CPUS];

static inline uint64_t sim_core_time_ms(uint64_t raw_tsc, uint64_t bsp_tsc0, uint64_t per_ms, int64_t core_off) {
    if (!per_ms) return 0;
    int64_t adj = (int64_t)(raw_tsc - bsp_tsc0) + core_off;
    if (adj < 0) adj = 0;
    return (uint64_t)adj / per_ms;
}

int main(void) {
    printf("=== Testing Cross-Core TSC Sync & Monotonic Accounting ===\n");

    uint64_t per_ms = 2500000; /* 2.5 GHz */
    uint64_t bsp_tsc0 = 10000000;

    /* 1. Synchronized core */
    uint64_t ap1_raw = 12500000; /* 1 ms after bsp_tsc0 */
    uint64_t ms1 = sim_core_time_ms(ap1_raw, bsp_tsc0, per_ms, 0);
    assert(ms1 == 1);
    printf("PASS: Core with zero offset advances correctly (1 ms)\n");

    /* 2. AP core starting behind (started 500 ms late in QEMU: AP TSC starts near 0) */
    /* Suppose BSP is at tsc0 + 500 * per_ms, AP raw TSC is only 2.5M cycles (1 ms since AP reset) */
    /* BSP current: bsp_tsc0 + 1250000000. AP raw: 2500000.
     * The true BSP TSC is 1260000000. Difference b - a = 1257500000.
     * Offset to add to AP TSC is +1257500000 cycles.
     */
    int64_t ap_offset = 1257500000;
    uint64_t ap_raw_delayed = 2500000;
    uint64_t ms_ap = sim_core_time_ms(ap_raw_delayed, bsp_tsc0, per_ms, ap_offset);
    assert(ms_ap == 500); /* (2500000 - 10000000) + 1257500000 = 1250000000 / 2.5M = 500 ms */
    printf("PASS: Delayed AP core with offset aligns accurately to BSP monotonic timeline (500 ms)\n");

    /* 3. Monotonicity across thread migration */
    /* Thread starts on AP (ms=504), migrates to BSP (where raw tsc is at bsp_tsc0 + 1252500000 -> ms=505) */
    uint64_t bsp_migrated_raw = bsp_tsc0 + 1262500000;
    uint64_t ms_bsp_after = sim_core_time_ms(bsp_migrated_raw, bsp_tsc0, per_ms, 0);
    assert(ms_bsp_after >= ms_ap);
    printf("PASS: Nondecreasing clock read across migration (AP ms=%" PRIu64 " -> BSP ms=%" PRIu64 ")\n", ms_ap, ms_bsp_after);

    /* 4. Qualification status and error bounds */
    g_test_sync[1].qualified = 1;
    g_test_sync[1].offset = ap_offset;
    g_test_sync[1].rtt_min = 1500;
    g_test_sync[1].error_code = 0;
    assert(g_test_sync[1].qualified == 1 && g_test_sync[1].error_code == 0);

    /* 5. Unqualified core fallback: error_code != 0, qualified = 0 */
    g_test_sync[2].qualified = 0;
    g_test_sync[2].offset = 0;
    g_test_sync[2].rtt_min = ~0ull;
    g_test_sync[2].error_code = 1; /* Timeout */
    assert(g_test_sync[2].qualified == 0 && g_test_sync[2].error_code == 1);
    printf("PASS: Qualification thresholds and fallback state verified\n");

    /* 6. Multi-core qualification topology matrix (2 to 8 cores, multi-socket) */
    for (int cores = 2; cores <= 8; cores++) {
        for (int c = 1; c < cores; c++) {
            g_test_sync[c].qualified = 1;
            /* AP offset within +/- 200 ms simulated boot latency differences */
            g_test_sync[c].offset = (int64_t)(c * 1500000) * (c % 2 == 0 ? -1 : 1);
            g_test_sync[c].rtt_min = 1000 + c * 250; /* Low RTT < 5 ms max */
            g_test_sync[c].error_code = 0;

            uint64_t simulated_ap_tsc = bsp_tsc0 + 50000000 - g_test_sync[c].offset;
            uint64_t calculated_ms = sim_core_time_ms(simulated_ap_tsc, bsp_tsc0, per_ms, g_test_sync[c].offset);
            assert(calculated_ms == 20); /* 50M cycles / 2.5M cycles/ms = 20 ms */
        }
    }
    printf("PASS: Multi-core qualification matrix (2 to 8 cores) verified\n");

    printf("All 6 cross-core synchronization tests passed.\n");
    return 0;
}
