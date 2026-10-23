# Opt-in boot-core latency diagnostics

`latency on` resets a session, `latency status` writes its histogram counts,
upper-bound p95 values, maxima, drops, memory/swap and CPU utilization to the
serial log; `latency off` stops collection and prints the last sample.

The instrumented domain is the boot core, not all CPUs. Keyboard metrics cover
the instrumented keyboard queues/polls, not complete hardware-to-photon latency.
IRQ masking through io.h, spinlock.h and task.c is measured; raw cli sites are
not exhaustively covered. Hardware handler chains and timer intervals are
sampled too. No allocation, logging, yielding or lock acquisition occurs in
the measurement hooks. Sampling is disabled by default.

Use the current shared timer_counter (HPET/ACPI PM), not the fork's historical
TSC clock or a new TSC synchronization path. IRQ-only fallback cannot measure
IRQ-disabled time and produces no timing samples. Histogram bounds/saturation,
nested IRQ sections, reset, CPU filtering and clock wrap are covered by host
tests of the actual module. Runtime overhead under load and physical hardware
input/interrupt qualification remain measurements to perform before claiming
performance improvements; this change supplies diagnostics, not such a claim.
