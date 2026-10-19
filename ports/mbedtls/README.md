# Banana Mbed TLS port foundation (base-01 step 1)

The top-level Makefile compiles and links the pinned Mbed TLS source and
platform adapters into both i386 and x86_64 kernels. This is an internal kernel
interface, with no SDK ABI, disk-format or durability-contract change.
`net/tls.c`, `net/tls12.c` and existing HTTPS consumers are still unchanged and
still unauthenticated. This step alone does not secure HTTPS.

The initial profile is TLS 1.2 client only: ECDHE-RSA/ECDHE-ECDSA and
ChaCha20-Poly1305, SHA-256/384, P-256/P-384/X25519, RSA, PEM/DER X.509 parsing,
key usage, extended key usage, and UTC certificate date checks. The scalar profile excludes table-based software AES following upstream
[security guidance](../../third_party/mbedtls/SECURITY.md); kernel interrupt
handling does not preserve SIMD registers, so enabling AES-NI needs separate
architecture work. Servers must offer a ChaCha20-Poly1305 suite. No TLS 1.0/1.1,
static RSA key exchange, CBC, PSK, DTLS, filesystem, host sockets, session
persistence, early data or host entropy fallback is enabled. TLS 1.3/PSA support
can be evaluated separately; existing TLS code is not used as a fallback by
this port. Upstream default client auth mode is VERIFY_REQUIRED. The CA bundle
and a consumer enforcing hostname, trust and handshake completion belong to
steps 2 and 3, including the full negative certificate matrix.

## Platform contracts

- `banana_tls_calloc` rejects multiplication overflow and zero allocations,
  uses zeroed kernel heap memory, propagates allocation failure, and pairs with
  `banana_tls_free`. Upstream handles secure zeroization of its secret state.
- `mbedtls_hardware_poll` uses only `banana_tls_seed`: CPUID-gated RDSEED with
  bounded retries, no RDRAND, timestamps, MAC address, or `random_bytes` fallback.
  A failure reports no credited output and causes Mbed TLS entropy/DRBG seeding
  to fail. The RDSEED instruction's platform trust is an explicit assumption;
  CPU advertisement alone is not physical hardware validation. Systems and
  QEMU CPU profiles without RDSEED cannot seed this port. A reviewed alternative
  entropy source is a proposed checklist follow-up. Never mark the legacy
  kernel timing pool as a strong source merely to make a handshake succeed.
- Initialize one `mbedtls_entropy_context` and `mbedtls_hmac_drbg_context` per
  connection; seed SHA-256 HMAC-DRBG via `mbedtls_entropy_func`, check the return,
  and install `mbedtls_hmac_drbg_random` using `mbedtls_ssl_conf_rng`. Free both
  contexts on all exits. Contexts and the transport must have one owner; they
  are not shared across tasks. There is no global mutable Mbed TLS RNG state.
- `banana_tls_time` reads `rtc_now_utc`, never local `rtc_now`. Missing RTC returns
  -1. The UTC conversion rejects missing/out-of-range timestamps; Mbed TLS date
  predicates then fail closed. This supports the RTC's current 32-bit Unix-time
  range through 2106, not arbitrary 64-bit dates. A plausible but incorrect RTC
  remains possible; this port does not establish trusted network time. Users
  must set the correct UTC/RTC convention and timezone. There is no automatic
  switch that disables date validation on clock failure.
- The Mbed TLS millisecond hook uses `timer_ms64`; the existing `timer_ms` keeps
  its 32-bit API. The extended PIT fallback prevents a torn i386 read by masking
  IRQs on the kernel CPU. No network deadlines are changed.
- `banana_tls_transport` borrows a connected TCP object, timeout and original
  network error. Install `banana_tls_send`/`banana_tls_recv` with
  `mbedtls_ssl_set_bio`. Callbacks block, bound byte counts to INT_MAX, preserve
  EOF as zero, and translate timeout/reset/other errors. A timeout is fatal to
  the current operation; callers must not loop indefinitely. TCP send can queue
  partial bytes before failing: abandon a failed TLS connection instead of
  retrying its plaintext. Each callback timeout is per TCP call, not a total
  handshake deadline. Consumer integration must impose the overall deadline.
  Mbed TLS close/free does not close or free TCP.

## Verification

`sh tools/mbedtlstest/run.sh` compiles the exact port profile and executes host
regressions on x86_64 (`ARCHES="32 64"` also runs i386 where host
execution permits its syscalls), using a deterministic **test-only** seed source,
fake TCP results and fake RTC input with the kernel's actual civil-date routines.
It checks allocator overflow/OOM/zeroing, leap-date UTC conversion, missing and
changed clock behavior, DRBG entropy failure/success, transport errors/EOF/length
bounds and TLS client setup/handshake transport failure. It does not prove real
RDSEED quality, RTC hardware behavior, full handshake interoperability, or a
verified HTTPS request. No test-only source is linked into the kernel.

Targeted cross-build and relocatable-link dependency check:
`sh tools/mbedtlstest/cross.sh`. This also compiles the extended kernel timer.
Full kernel build: `make -j6 kernel.bin kernel64.bin` (includes application dependencies).
No QEMU or physical hardware TLS validation is claimed by these commands.
