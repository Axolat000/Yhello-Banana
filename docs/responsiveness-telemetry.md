# Responsiveness & Latency Telemetry

## Overview
To monitor and address system responsiveness under load (e.g. during disk copy, network downloads, audio playback, and low-memory conditions), boot-core responsiveness telemetry has been implemented.

## Shell Command
```sh
latency [on | off | status]
```
- `latency on`: Resets counters and begins a boot-core telemetry collection session.
- `latency off`: Disables telemetry and logs a final summary report to the serial console (`klog`).
- `latency status` (or `latency`): Emits the current telemetry metrics immediately to the serial console.

## Telemetry Metrics
When active, `latency_poll()` emits a telemetry record to the serial console every second (`1000 ms`). The telemetry record covers:
- **Input latency (`input_p95_le_us`, `input_max_us`, `drops`)**: Time from hardware keypress interrupt receipt to userspace terminal consumption.
- **Interrupt latency & duration (`irq_p95_le_us`, `irq_max_us`)**: Time spent in non-interruptible sections / hardware interrupt handling on the boot core.
- **Poll & Timer latency (`poll_p95_le_us`, `poll_max_us`, `timer_p95_le_us`, `timer_max_us`)**: Interval jitter between input polling loops and system timer ticks.
- **Memory metrics**: Total RAM, free RAM, available RAM, and largest contiguous free block via `meminfo_get()`.
- **Swap metrics**: Compressed swap pages/bytes in zswap, disk swap usage, and in/out page activity via `zswap_stats()`.
- **CPU time per core**: CPU busy percentage and accumulated busy time in milliseconds per CPU core (`task_core_pct`, `task_core_ms`).

## Validation
- Host-level unit tests covering histogram bucketing, P95 calculations, interrupt begin/end accounting, and poll period filtering are in `tools/tests/latency.c` executed via `tools/test_latency.sh`.
