# Monotonic Clock, UTC Realtime, and Network Time Synchronization Characterization

This document details the clock architecture, time separation, time adjustment models, and network time synchronization bounds for Banana OS, delivering **base-17 step 3** of the living checklist.

---

## 1. Executive Summary & Architectural Overview

Banana OS maintains two fundamentally distinct notions of time:

1. **Monotonic Clock (`timer_ms()`, `timer_ticks()` in `kernel/timer.c`)**:
   - Driven by hardware: either invariant Time Stamp Counter (TSC calibrated via PIT Channel 2 during `timer_init()`) or PIT Channel 0 periodic 1 kHz interrupts (`IRQ0` counting `g_ms`).
   - Monotonically non-decreasing counter of elapsed milliseconds since system boot.
   - **Critical Contract**: It is never stepped backward or adjusted by wall-clock changes or time-zone shifts.
   - **Consumers**: All networking stack deadlines, RTT/RTO timers (`net/tcp.c`), ARP cache expiry (`net/arp.c`), DHCP lease deadlines (`net/dhcp.c`), DNS cache and query timeout tracking (`net/dns.c`), process scheduling, and POSIX `CLOCK_MONOTONIC` (`sdk/lib/libc.c`).

2. **Realtime / Wall Clock UTC (`rtc_now_utc()`, `rtc_now()` in `kernel/rtc.c`)**:
   - Represents civil / calendar date and time.
   - Bootstrapped from the battery-backed CMOS Real-Time Clock (MC146818 compatible ports `0x70`/`0x71`).
   - Cached in memory as `base` (seconds since Unix epoch 1970-01-01 00:00:00 UTC) at timestamp `base_ms` (reading `timer_ms()`).
   - Can be configured to store local civil time (Windows style) or UTC (Linux style) in hardware CMOS via `rtc_set_keeps_utc()`.
   - Accessible to userspace via `banana_api_t::time_utc` and POSIX `CLOCK_REALTIME` / `gettimeofday()`.
   - **Mutability**: Subject to manual user adjustments (`date set`, Settings GUI) and automated synchronization (hypervisor QGA `guest-set-time`, VM tools backdoor, or NTP/SNTP network time synchronization).

---

## 2. In-Depth Subsystem Analysis

### 2.1 Monotonic Clock Integrity (`kernel/timer.c`)
- `timer_init()` sets PIT mode 2 rate generator at 1000 Hz (`PIT_HZ`).
- `calibrate_tsc()` verifies TSC stability using CPUID invariant TSC flags (`0x80000007` bit 8) or hypervisor presence (`CPUID.1:ECX[31]`).
- `timer_ms()` computes `(tsc_now() - g_tsc0) / g_tsc_per_ms` when TSC is calibrated and invariant, otherwise falls back to interrupt-driven `g_ms`.
- Crucially, `timer_ms()` does not query CMOS RTC or any wall-clock variables.

### 2.2 Wall Clock Anchoring & Slewing vs Stepping (`kernel/rtc.c`)
- In `kernel/rtc.c`, `raw_now()` calculates:
  $$\text{now} = \text{base} + \frac{\text{timer\_ms}() - \text{base\_ms}}{1000}$$
- CMOS is re-read every 600,000 ms (10 minutes) or upon cold boot.
- When `rtc_set_utc(new_utc)` is called:
  - Writes new datetime to CMOS RTC registers (`0x00`-`0x09`).
  - Updates `base = t` and re-anchors `base_ms = timer_ms()`.
  - **Notice**: This modifies only `base` and `base_ms`. It does **NOT** modify `g_ms`, `g_tsc0`, or `timer_ms()`.

### 2.3 POSIX Realtime Subsystem (`sdk/lib/libc.c`)
- POSIX `clock_gettime(CLOCK_MONOTONIC, &ts)` directly translates `__banana->ticks_ms()` into seconds and nanoseconds.
- POSIX `clock_gettime(CLOCK_REALTIME, &ts)` queries `__realtime_ms()`.
- `__realtime_ms()` uses a local anchor `base + ms`. If the drift between `time(0) * 1000` and `now` exceeds 2000 ms (e.g. following a step or time-zone change), it re-anchors.
- For small adjustments, slewing avoids sudden discontinuities in POSIX applications.

---

## 3. TCP Delay & Network Deadline Invariance Analysis

The central requirement of base-17 step 3 is:
> *"Définir une synchronisation réseau et une correction d’heure qui ne casse pas les délais TCP."*
> (Define network time synchronization and time correction that does not break TCP delays.)

### 3.1 Why Stepping Wall Time Breaks Naive Stacks
In naive OS network stacks where socket timeouts, retransmission deadlines, or round-trip-time calculations reference `time()` or `CLOCK_REALTIME`:
- **Forward Step (+1 hour, e.g. DST change or NTP step sync)**:
  - Any active connection's deadline ($t_{\text{deadline}} = t_{\text{start}} + \text{timeout}$) is instantaneously passed:
    $$\Delta t = t_{\text{wall}} - t_{\text{deadline}} > 0$$
  - Results in spurious timeouts, aborted TCP connections (`NET_ERR_TIMEOUT`), mass SYN retransmissions, and broken active downloads.
- **Backward Step (-10 seconds or -1 hour)**:
  - Deadlines appear far in the future.
  - Retransmission timers (`c->rto_deadline`) freeze; packet loss is not detected for the entire duration of the backward jump.
  - RTT measurement calculation `now - rtt_start` underflows (or becomes enormous), breaking Van Jacobson's RTO calculation algorithm.

### 3.2 Banana OS TCP Verification (`net/tcp.c`)
In `net/tcp.c`, all TCP timers strictly use `timer_ms()`:
```c
// net/tcp.c
static void arm_timer(tcp_conn_t* c) {
    c->timer_on = 1;
    c->rto_deadline = timer_ms() + c->rto;
}

void tcp_timer(void) {
    uint32_t now = timer_ms();
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        tcp_conn_t* c = &g_conns[i];
        if (!c->used) continue;
        if (c->state == TCP_TIME_WAIT && (int32_t)(now - c->tw_deadline) >= 0)
            c->state = TCP_CLOSED;
        if (c->timer_on && c->state != TCP_CLOSED && (int32_t)(now - c->rto_deadline) >= 0)
            on_timeout(c);
        ...
    }
}
```
Because `timer_ms()` is completely decoupled from `rtc_set_utc()`, `rtc_set_time()`, and `raw_now()`:
1. Stepping UTC forward by any amount (e.g. +3600s, +1 day) leaves `timer_ms()` unaffected. `rto_deadline - timer_ms()` does not jump.
2. Stepping UTC backward by any amount (e.g. -3600s, -100s) leaves `timer_ms()` unaffected. RTT calculations (`timer_ms() - c->rtt_start`) remain strictly accurate.
3. TCP connection state machines, zero-window probes, `TIME_WAIT` timers (2MSL / 60s), and socket blocking timeouts (`tcp_recv` / `tcp_send`) suffer **zero interruption or premature expiration**.

---

## 4. Network Time Synchronization Specification (NTP / SNTP RFC 5905 & 4330)

Per the base-17 decision guidance:
> *"Le client NTP, le fallback DNS TCP et les sockets IPv6 doivent être traités comme sous-livrables distincts, selon le résultat de l’inventaire."*

The following architecture and protocol specification governs the standalone NTP client proposed for follow-up scheduling (`base-17-ntp`):

### 4.1 Packet Structure & Port
- Transport: UDP port 123 (both source and destination, or ephemeral source).
- Header: Standard 48-byte NTP packet.
  - Byte 0: `LI = 0` (no warning), `VN = 4` (NTPv4), `Mode = 3` (Client) -> `0x23` (`0b00100011`).
  - Bytes 1-3: Stratum (0), Poll (4), Precision (0).
  - Bytes 4-7: Root Delay (0).
  - Bytes 8-11: Root Dispersion (0).
  - Bytes 12-15: Reference ID (0).
  - Bytes 16-23: Reference Timestamp (0).
  - Bytes 24-31: Origin Timestamp (0).
  - Bytes 32-39: Receive Timestamp (0).
  - Bytes 40-47: Transmit Timestamp $T_1$ (client transmit time in NTP epoch format: seconds since 1900-01-01 00:00:00 UTC, with 2,208,988,800s epoch offset from Unix 1970).

### 4.2 Time Calculation
Given:
- $T_1$: Client transmit monotonic timestamp (`timer_ms()`) and transmit wall time.
- $T_2$: Server receive timestamp.
- $T_3$: Server transmit timestamp.
- $T_4$: Client receive monotonic timestamp (`timer_ms()`).

Round-Trip Delay:
$$\delta = (T_4 - T_1) - (T_3 - T_2)$$
Offset:
$$\theta = \frac{(T_2 - T_1) + (T_3 - T_4)}{2}$$

### 4.3 Time Correction Discipline: Step vs Slew
To guarantee application and filesystem stability without breaking monotonically advancing timers:
1. **Initial / Large Offset ($|\theta| > 128 \text{ ms}$ or $|\theta| > 1 \text{ s}$)**:
   - Perform a step adjustment via `rtc_set_utc(target_utc)`.
   - Update CMOS RTC and base anchor.
   - Verified: TCP deadlines remain completely unaffected because `timer_ms()` is decoupled.
2. **Small Offset ($|\theta| \le 128 \text{ ms}$)**:
   - Slew adjustment (frequency scaling or gradual fractional tick bias of $\pm 500 \text{ PPM}$).
   - Guarantees `CLOCK_REALTIME` monotonic progression without step discontinuities.

### 4.4 Failure Modes & Call Bounds

| Scenario | Behavior | Bound / Return | Impact on TCP / Networking |
|---|---|---|---|
| **NTP Server Unreachable / Loss** | 3 UDP retransmissions at 2-second intervals | Fails with `NET_ERR_TIMEOUT` after 6000 ms | None; TCP continues on monotonic clock |
| **Malformed NTP Packet (< 48 bytes)** | Length check in packet validator | Packet dropped; retry or timeout | None |
| **Unsynchronized Server (`LI == 3` or `Stratum == 0`)** | Validation rejects response | Ignored as unsynchronized | None |
| **Large Step Forward (+1 hr DST / Jump)** | `rtc_set_utc()` re-anchors `base` | Completed in < 1 ms | **No impact on TCP timers or deadlines** |
| **Large Step Backward (-1 hr / Jump)** | `rtc_set_utc()` re-anchors `base` | Completed in < 1 ms | **No impact on TCP timers or deadlines** |
| **CMOS Write Guard Exhaustion** | 10000 iteration loop in `cmos_datetime` | Fails with -1, retains memory anchor | None |

---

## 5. Host Test Verification Suite

A comprehensive test suite in `tools/tests/clock_monotonic_utc.c`, `tools/tests/test_ntp_client.c`, and `tools/test_clock_monotonic_utc.py` verifies:
1. Complete isolation between `timer_ms()` and `rtc_set_utc()` / `rtc_set_time()`.
2. TCP deadline math invariance under large positive and negative UTC steps:
   - $RTO$ countdowns remain strictly monotonic.
   - Retransmission deadlines do not fire prematurely when UTC steps forward.
   - Retransmission deadlines do not stall when UTC steps backward.
   - Round-trip time (RTT) estimations remain unaffected by wall-clock changes.
3. NTP packet serialization, epoch conversion (1900 vs 1970 epoch delta: 2,208,988,800 s), and offset calculation formulas in `net/ntp.c` and `net/ntp.h`.
4. Live UDP mock NTP server query/reply exchanges validating stratum 1 responses, millisecond resolution delay and offset calculation, and error rejection (truncated packets, Kiss-o'-Death, unsynchronized leap indicator).
5. POSIX `CLOCK_MONOTONIC` vs `CLOCK_REALTIME` behavior under simulated time steps.
