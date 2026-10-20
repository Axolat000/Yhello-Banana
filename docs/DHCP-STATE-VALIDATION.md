# DHCP state display validation

Scope: base-17 step 1 only, on source base `fe73f98`. DHCP protocol behavior,
ABI, disk formats, licences and durability contracts are unchanged. The source
change exposes DHCP state and the driver's link report in Network Settings;
the documentation records existing behavior and gaps. The checklist adds only
`base-17-dhcp-lifecycle`, a proposal requiring independent review and controller
validation. All pre-existing entries (including base-17 steps) are unchanged.

Execution on the Debian build host, 2026-10-19:

| Check | Observed result | Local log |
| --- | --- | --- |
| `python3 tools/test_dhcp_state.py --sanitize` | Passed: 267 assertions; real DHCP implementation and extracted Settings renderer, native 64-bit with ASan/UBSan. `KNOWN GAP` cases characterize existing defects, not successful recovery. | `build/logs/base-17-host64.log` |
| `python3 tools/test_dhcp_state.py --32` | Compilation succeeded; execution terminated with SIGSYS. No 32-bit runtime pass claimed. | `build/logs/base-17-host32.log` |
| Freestanding compilation of complete `kernel/settings.c`, i386 and x86_64 | Both compiler commands exited 0, no diagnostics. Objects are in ignored `build/test/dhcp/settings32.o` and `settings64.o`, not the deliverable. | `build/logs/base-17-settings-compile.log` |
| Checklist structure/preservation check (Python JSON parse and assertions) | Passed: all original entries equal HEAD, unique IDs, dependencies resolve, required planning fields present. | `build/logs/base-17-checklist.log` |

The Antigravity helper was invoked once with a bounded request for a proposed
patch/documentation/regression harness. It rejected the request with
`Configuration must be root-owned and not group/world writable`. No proposal
was produced, no configuration was changed, and implementation/verification
continued locally.

The host renderer test captures labels and colors, not pixels. Physical NIC
validation and the controlled short-lease/server-silence/30-second carrier
outage/download qualification were not performed. They remain explicit
follow-up acceptance work in the checklist and DHCP failure matrix. This
substep does not mark base-17 or the daily-use OS goal complete.
