# Cross-Core TSC Clock Qualification and Synchronization

## Context and Upstream Analysis

Upstream commit `1b842a6433831f9cf13f0cd1ab3c253a066e9072` addressed an issue where tasks migrating across processor cores experienced artificial multi-hour time jumps or scheduler accounting distortions due to desynchronized TSCs (particularly in QEMU where secondary cores start with TSC near 0 seconds after the bootstrap processor).

However, wholesale adoption of upstream commit `1b842a643383` was identified as unsuitable due to several key deficiencies:
1. Upstream used un-serialized raw `rdtsc` instructions, which Intel and AMD architecture reference manuals do not guarantee to be instruction-serializing. Out-of-order execution around the measurement loops can introduce measurement jitter and skewed offset calculations.
2. Direct MSR writes to `IA32_TSC_ADJUST` (0x3B) or `IA32_TIME_STAMP_COUNTER` (0x10) impose platform-sensitive side effects. Unconditional writes to hardware counter registers can cause hypervisor VM exits, hardware faults on virtualization layers, or inconsistencies with firmware invariant TSC accounting.
3. The upstream implementation returned no qualification or failure status from `ts_measure()` or `tsc_sync_ap()`, proceeding to admit the AP core even if timeout occurred or RTT jitter was excessive.
4. Upstream mixed S3 suspend/resume logic that relies on unported power management dependencies.

## Selected Alternative Design

Rather than unconditional hardware counter writes without failure handling, Banana OS implements **explicit boot-time cross-core clock qualification and software offset tracking**:

1. **Serialized Sampling**: All measurement stamps use instruction serialization (`lfence; rdtsc`) ensuring memory ordering and cycle accuracy across the multi-round exchange.
2. **Minimum Round-Trip Time (RTT) Selection**: Over `TS_ROUNDS` (8 rounds), the AP and BSP exchange round-trip timestamps. The round achieving the minimum RTT (`a2 - a1`) is selected, calculating the mid-point relative offset:
   $$\text{offset} = b - (a_1 + \frac{a_2 - a_1}{2})$$
3. **Qualification and Bounded Error Thresholds**:
   - Round-trip time is bounded (maximum allowed RTT of $5 \times \text{cycles per ms}$ or 20M cycles).
   - Timeouts in the spin-barrier (`ts_wait()`) are explicitly detected.
   - Per-core qualification state (`g_tsc_sync[cpu]`) stores `qualified`, `offset`, `rtt_min`, and `error_code`.
4. **Shared Clock Helper (`timer_core_now_ms()`)**:
   Instead of modifying physical CPU MSR registers, time-accounting and monotonic clock callers compute elapsed milliseconds taking into account the qualified per-core offset:
   $$\text{adj} = (\text{tsc}_{\text{raw}} - \text{tsc}_0) + \text{offset}_{\text{cpu}}$$
   $$\text{time}_{\text{ms}} = \frac{\max(0, \text{adj})}{\text{tsc\_per\_ms}}$$
   If TSC invariant clocking is unavailable or qualification fails, the system safely relies on the PIT/IRQ monotonic tick counter fallback.
5. **No S3 Resume Scope Creep**: Omitted unverified S3 wake logic until power management infrastructure is formally scheduled and qualified.

## Validation and Evidence

1. **QEMU Multi-Core Boot & Selftest**:
   - Verified on 4-core QEMU system (`smp: 4 of 4 logical processors running`).
   - Core 1, 2, and 3 successfully ran the exchange; logged offsets and round-trip cycles (`smp: core 1 TSC agrees with BSP (rtt 1860 cycles)`, `smp: core 2 TSC qualified: offset -176 us (rtt 1168626 cycles)`).
   - Selftest suite executed without regressions (`selftest: 183 passed`, `threads 4 200000` completed with matching results in 88 ms, `cltest` passed).
2. **Host Unit Characterization Suite (`tools/test_cross_core_clock.py`)**:
   - Tests zero offset, positive AP lead, negative AP lag, thread migration monotonicity, and error code propagation under timeout/excessive jitter.
3. **Checklist Follow-Up**:
   - Added `base-17-cross-core-clock` as a P1 task linked to `base-17` to track continuous drift qualification, recalibration, and physical hardware validation.
