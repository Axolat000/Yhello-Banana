# Supported CPU and QEMU Entropy Capabilities & Trust Model

## 1. Context and Objective

The Banana OS Mbed TLS port ([ports/mbedtls/README.md](file:///var/lib/yhello/worktrees/base-01-entropy-s1/ports/mbedtls/README.md)) previously relied solely on Intel/AMD `RDSEED` via `banana_tls_seed` in [ports/mbedtls/banana_seed.c](file:///var/lib/yhello/worktrees/base-01-entropy-s1/ports/mbedtls/banana_seed.c) to seed the HMAC-DRBG random number generator.
When `RDSEED` is missing or fails:
- Seeding fails immediately (`MBEDTLS_ERR_ENTROPY_SOURCE_FAILED`).
- No fallback to uncredited timing/jitter pools ([kernel/random.c](file:///var/lib/yhello/worktrees/base-01-entropy-s1/kernel/random.c)) occurs.
- Authenticated HTTPS (`tls_connect` in [net/tls.c](file:///var/lib/yhello/worktrees/base-01-entropy-s1/net/tls.c)) aborts before sending a `ClientHello`.

However, default QEMU virtual CPUs (`qemu64`, `kvm64`), older virtual machine configurations, and pre-Broadwell / pre-Zen physical x86 processors (such as Nehalem, Westmere, Sandy Bridge, Ivy Bridge, and Haswell) do not feature the `RDSEED` instruction (CPUID leaf 7, EBX bit 18). As a result, authenticated TLS cannot establish sessions on these platforms.

The goal of task `base-01-entropy` is to inventory supported hardware and virtualized entropy mechanisms, evaluate candidates against rigorous security and architectural constraints, select a maintained entropy acquisition mechanism with an explicit trust model, and define the contracts for error reporting and readiness.

---

## 2. Inventory of Candidate Entropy Sources

We evaluate candidate hardware and hypervisor entropy mechanisms across physical CPUs and QEMU virtualization environments:

| Mechanism | Platform / Availability | Hardware / Hypervisor Interface | Failure / Readiness Signaling | Viability for Banana OS TLS DRBG Seeding |
| :--- | :--- | :--- | :--- | :--- |
| **RDSEED** | Intel Broadwell+ (2014), AMD Zen+ (2017), QEMU `-cpu Skylake-Client+` or `-cpu host` | CPUID.(EAX=07H, ECX=0H):EBX[bit 18]; instruction `rdseed %reg; setc %reg` | Carry flag (`CF=1` success, `CF=0` underflow/transient failure). | **Primary physical hardware source** (already implemented in `banana_seed.c`). Fails on older CPUs and default QEMU profiles. |
| **RDRAND** | Intel Ivy Bridge+ (2012), AMD Jaguar / Zen+, QEMU `-cpu IvyBridge+` | CPUID.(EAX=01H):ECX[bit 30]; instruction `rdrand %reg; setc %reg` | Carry flag (`CF=1` success, `CF=0` underflow/failure). | Cryptographic DRBG (NIST SP 800-90A) conditioned from on-chip TRNG. High throughput, widely supported on x86_64, but absent on `qemu64`, `kvm64`, Nehalem, and Westmere. Not sufficient by itself for default QEMU CPUs. |
| **virtio-rng** | All QEMU / KVM / Proxmox / cloud hypervisors via `-device virtio-rng-pci` | PCI standard vendor `0x1AF4`, device ID `0x1005` (legacy) / `0x1044` (modern virtio 1.x) | Virtqueue buffer completion with explicit byte count; timeout detection. | **Selected virtualization source**. Backed by host `/dev/urandom` or host hardware RNG. Fully compatible with QEMU across all CPU models (`-cpu qemu64, -cpu kvm64`, etc.). |
| **TPM 1.2 / 2.0 (CRB / TIS)** | Physical TPM chip (LPC/MMIO `0xFED40000`), QEMU `tpm-crb` / `tpm-tis` | MMIO command/response registers (`TPM2_GetRandom` / `TPM_GetRandom`) | Status register response code and returned byte count. | Requires extensive TPM state machine, startup handshake, and complex command marshaling. High overhead and fragile on virtualized setups without pass-through. Deferred. |
| **PIT / HPET / TSC Timing Jitter** | All physical x86 systems and virtual machines | Reading high-resolution counters (`rdtsc`, port `0x61`, HPET) during jitter loops | No hardware error contract. Cannot guarantee min-entropy across diverse hypervisors or emulators. | **Rejected for TLS seeding**. QEMU and virtualization engines may synchronize or virtualize TSC/timers deterministically. Violates acceptance criteria ("no timing-only fallback"). |

---

## 3. Comparative Evaluation and Gap Analysis

### 3.1 The Default QEMU Problem
In QEMU, the default x86_64 CPU is `qemu64`. The `qemu64` model advertises neither `RDSEED` nor `RDRAND`. Similarly, `kvm64` and older Intel CPU models (`Nehalem`, `Westmere`, `SandyBridge`, `Haswell`) lack `RDSEED`.

Running automated testing, Continuous Integration (CI), or deployment in default cloud environments without `-cpu host` or explicitly tuned CPU models causes `banana_tls_seed` to return `-1`, completely blocking TLS handshakes.

### 3.2 Virtio Infrastructure in Banana OS
Banana OS already possesses mature, reviewed modern and legacy virtio infrastructure:
- [kernel/virtio_pci.h](file:///var/lib/yhello/worktrees/base-01-entropy-s1/kernel/virtio_pci.h) & [kernel/virtio_pci.c](file:///var/lib/yhello/worktrees/base-01-entropy-s1/kernel/virtio_pci.c): Transport abstraction handling legacy I/O BARs and modern PCI capability BARs (`VIO_S_ACK`, `VIO_S_DRIVER`, `vio_queue`, `vio_notify`).
- Existing drivers utilizing `vio_t`:
  - [kernel/virtio_blk.c](file:///var/lib/yhello/worktrees/base-01-entropy-s1/kernel/virtio_blk.c) (storage)
  - [kernel/virtio_serial.c](file:///var/lib/yhello/worktrees/base-01-entropy-s1/kernel/virtio_serial.c) (QEMU guest agent)
  - [kernel/gpu_virtio.c](file:///var/lib/yhello/worktrees/base-01-entropy-s1/kernel/gpu_virtio.c) (2D display)
  - [net/virtio_net.c](file:///var/lib/yhello/worktrees/base-01-entropy-s1/net/virtio_net.c) (networking)

A dedicated `virtio-rng` driver fits cleanly into this architectural foundation.

---

## 4. Selected Architecture and Trust Model

### 4.1 Tiered Maintained Entropy Sources
Banana OS establishes a two-source credited entropy architecture for cryptographic DRBG seeding:

1. **Hardware Tier: CPU RDSEED (with fallback to RDRAND where verified)**:
   - For physical hardware platforms possessing hardware seed capabilities:
     - `RDSEED`: Provides non-cryptographically conditioned true physical entropy (NIST SP 800-90B/C compliant).
     - `RDRAND`: On CPUs with `RDRAND` (Ivy Bridge through Haswell) lacking `RDSEED`, `RDRAND` provides high-quality cryptographic seed material conditioned by on-chip AES-CBC-MAC.
   - Bounded retries: 128 attempts per 32-bit word with CPU `pause`.
   - On underflow or failure, zeroize any collected seed bytes and report explicit failure.

2. **Hypervisor Tier: Paravirtualized Virtio-RNG (`virtio-rng-pci`)**:
   - For virtualized platforms (QEMU, KVM, Proxmox, cloud hypervisors) running with any CPU model (including `qemu64` and `kvm64`).
   - Device: PCI vendor `0x1AF4`, subsystem device `0x1005` (legacy) or `0x1044` (modern virtio 1.x).
   - Queue: Single receive virtqueue (`vq 0`). The guest places empty write-only buffers into the descriptor table, notifies the host, and awaits host completion.
   - The host hypervisor pulls credited entropy directly from `/dev/urandom` or the host system's hardware RNG.

### 4.2 Explicit Trust Model & Boundary Assumptions
1. **Hypervisor Integrity Assumption**:
   - When running inside a virtual machine, the guest inherently trusts the hypervisor for memory isolation, instruction execution, and virtual device emulation.
   - The hypervisor's RNG stream via `virtio-rng` is trusted to supply full-entropy bytes from a cryptographic randomness pool.
2. **Hardware CPU Instruction Assumption**:
   - Physical CPU instructions (`RDSEED` / `RDRAND`) are trusted according to vendor specifications. However, CPUID advertisement alone does not guarantee physical hardware authenticity; underflow must be handled without deadlocking.
3. **No Uncredited Fallback**:
   - Uncredited timing jitter, network packet arrivals, or legacy PRNG states from `kernel/random.c` **must never** be credited to satisfy TLS seeding requirements.
   - If neither `RDSEED` nor `virtio-rng` is available (or if communication times out / fails), the initialization **must fail closed**.
   - No `ClientHello` may ever be transmitted using weak or unseeded keys.

---

## 5. Interface Contract for Step 2

### 5.1 Kernel Entropy Acquisition Interface
To separate platform device handling from Mbed TLS, a unified credited entropy provider interface is established:

```c
typedef enum {
    ENTROPY_SRC_NONE       = 0,
    ENTROPY_SRC_RDSEED     = 1,
    ENTROPY_SRC_RDRAND     = 2,
    ENTROPY_SRC_VIRTIO_RNG = 3
} entropy_source_t;

/* Status and capability probe */
int entropy_available(void);
entropy_source_t entropy_primary_source(void);

/* Bounded seed acquisition */
/* Returns 0 on success (all requested bytes supplied from credited sources),
 * or negative error code on failure/timeout.
 * On error, out buffer is guaranteed to be zeroized. */
int entropy_get_bytes(uint8_t* out, size_t len);
```

### 5.2 Mbed TLS Hook Integration
`banana_tls_seed` in [ports/mbedtls/banana_seed.c](file:///var/lib/yhello/worktrees/base-01-entropy-s1/ports/mbedtls/banana_seed.c) delegates to `entropy_get_bytes`:
- Checks `RDSEED` first. If present and successful, satisfies request.
- If `RDSEED` is absent, falls back to `virtio-rng` (or `RDRAND` if configured).
- If no credited source is present or capable of providing the requested length within bounded timeouts (100 ms), returns `-1`.
- Any partial read or failure triggers explicit zeroization of `out` and propagation of `MBEDTLS_ERR_ENTROPY_SOURCE_FAILED`.

---

## 6. Validation and Test Strategy (Step 3 Preview)

To prove compliance with the acceptance criteria:
1. **Negative Validation (No approved source)**:
   - Run QEMU with `-cpu qemu64` and without `-device virtio-rng-pci`.
   - Attempt TLS client connection. Must fail immediately during `mbedtls_hmac_drbg_seed` with error `NET_ERR_PROTO` or TLS setup error; zero TCP packets sent on port 443.
2. **Positive Validation (Virtio-RNG on non-RDSEED CPU)**:
   - Run QEMU with `-cpu qemu64` (no RDSEED) and `-device virtio-rng-pci`.
   - Probe confirms `virtio-rng` detected and active.
   - Seed succeeds and TLS handshake completes against an authenticated test server.
3. **Failure Injection**:
   - Host test harness and unit tests verifying that short reads, timeouts, or uninitialized buffers never report success or leave residue in caller memory.
