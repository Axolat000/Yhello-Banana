# base-01 step 1 verification, 2026-10-19

Prepared a pinned [Mbed TLS 3.6.7 port](../ports/mbedtls/README.md).
This is only the cross-build/platform foundation. Existing HTTPS consumers
remain unchanged and unauthenticated; CA installation and consumer migration
are still outstanding steps.

Execution evidence (local ignored logs retained in this worktree):

| Check | Observed result | Log |
| --- | --- | --- |
| `sh tools/mbedtlstest/cross.sh` | PASS, kernel flags for i386 and x86_64, relocatable port links, only kernel/libgcc undefined dependencies | `build/logs/base-01-s1/cross.log` |
| `sh tools/mbedtlstest/run.sh` | PASS, x86_64 allocator, UTC/date failure and restoration, entropy/DRBG failure, TCP mapping, TLS setup and handshake transport failure | `build/logs/base-01-s1/host64.log` |
| Initial i386 host attempt | Compiled and linked; execution terminated with Bad system call (159); no i386 runtime success claimed | `build/logs/base-01-s1/host32-attempt.log` |
| `make -j6 kernel.bin kernel64.bin` | Intentionally interrupted (130) while rebuilding application dependencies, including FFmpeg; no complete kernel build success claimed | `build/logs/base-01-s1/kernels-interrupted.log` |
| `git diff --check` | PASS for tracked changes; new port/test sources also checked for trailing whitespace | No separate log |
| Release source comparison | All 276 imported include/library files matched the official release archive byte for byte; archive SHA-256 matches upstream release notes | See `third_party/mbedtls/README.banana.md` |
| Checklist JSON | Parsed successfully, IDs unique, new dependency resolves | No separate log |

Host tests substitute deterministic test entropy and fake TCP/RTC providers;
they do not demonstrate RDSEED hardware quality, QEMU TLS interoperability,
physical hardware behavior, or the six negative certificate handshake cases.
The final profile offers TLS 1.2 ECDHE with ChaCha20-Poly1305 only. Systems without
RDSEED fail seeding; a separately reviewable entropy follow-up is proposed in
the living checklist. No source readiness fallback or date-verification bypass
was added.

Antigravity delegation was attempted once with the supplied helper. It rejected
execution with `Configuration must be root-owned and not group/world writable`.
No proposal was returned or applied; the temporary request was removed. The
helper/server configuration was not changed.

Handoff status: The base port foundation has been checkpointed in commit
`cc6f56ad6dca5e0667ec901560dd935cab3bc71e` and corrected for trailing EOF whitespace.
`git diff --check` passes cleanly against the base commit `fe73f98e2bf9aef877b4fdea5f8f73a724d6fd7c`.
Git metadata in the worker environment remains read-only by design; the explicit source
deliverables manifest is submitted for controller staging and independent review.
All cross-build and host harness test suites remain verified.
