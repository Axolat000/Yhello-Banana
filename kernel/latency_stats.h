#ifndef LATENCY_STATS_H
#define LATENCY_STATS_H

#include "types.h"

#define LATENCY_BINS 14
#define LATENCY_MAX 0xffffffffu

typedef struct {
    uint32_t bins[LATENCY_BINS], count, max_us;
} latency_hist_t;

/* Inclusive upper bounds in microseconds; the last bucket is unbounded. */
static const uint32_t latency_bounds[LATENCY_BINS] = {
    100, 250, 500, 1000, 2000, 5000, 10000,
    20000, 50000, 100000, 250000, 500000, 1000000, LATENCY_MAX
};

static inline void latency_hist_add(latency_hist_t* h, uint32_t us) {
    if (us > h->max_us) h->max_us = us;
    /* Freeze counts together on saturation, preserving percentile arithmetic. */
    if (h->count == LATENCY_MAX) return;
    unsigned i = 0;
    while (i + 1 < LATENCY_BINS && us > latency_bounds[i]) i++;
    h->bins[i]++;
    h->count++;
}

static inline uint32_t latency_hist_p95(const latency_hist_t* h) {
    if (!h->count) return 0;
    uint32_t rank = h->count - h->count / 20u, n = 0;
    for (unsigned i = 0; i < LATENCY_BINS; i++) {
        n += h->bins[i];
        if (n >= rank) return latency_bounds[i];
    }
    return LATENCY_MAX;
}

static inline uint32_t latency_elapsed_us(uint64_t start, uint64_t end, uint64_t per_ms) {
    if (!per_ms || end < start) return 0;
    uint64_t delta = end - start;
    /* Divide before multiplying; avoid overflow even for a very stale stamp. */
    uint64_t ms = delta / per_ms;
    if (ms > LATENCY_MAX / 1000u) return LATENCY_MAX;
    uint64_t us = ms * 1000u + (delta % per_ms) * 1000u / per_ms;
    return us > LATENCY_MAX ? LATENCY_MAX : (uint32_t)us;
}

#endif
