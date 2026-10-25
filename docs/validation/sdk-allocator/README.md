# SDK allocator review evidence

Executed on 2026-10-25, approximately 03:02–03:11 UTC, in the task worktree.
The starting revision was `bdfdf66735671c825df023537d8d06d12f1a9803`.
The tested source includes this retry's uncommitted high-memory test correction;
the starting commit alone does **not** identify the tested source.
`tested-sources.sha256` fingerprints the SDK sources/headers, changed runtime
sources, build integration and test inputs. Verify it from the repository root:

```sh
sha256sum -c docs/validation/sdk-allocator/tested-sources.sha256
```

`tested-artifacts.sha256` identifies the ISO, kernels and systest package used
for the runs. These binaries are intentionally excluded from the delivery;
tracked generated binaries are restored after testing. Rebuild before replaying
the commands. `environment.txt` records the compiler, linker, Python and QEMU
versions. All commands below ran from the repository root and exited zero.
Logs are retained here as source-review evidence, rather than referring to
ephemeral `/tmp` files. Terminal escape codes, carriage returns and trailing
whitespace are normalized in the text logs; no test-result lines are omitted.

## Host and builds

| Command | Evidence | Observed result |
| --- | --- | --- |
| `python3 tools/test_malloc.py` | `host.txt` | Zero failures; randomized preservation, eight-thread first use/cross-thread frees, VM failure injection, huge alignments and four API fallbacks; zero live/reserved bytes at completion. Linux VM/lock/thread substitutes only. |
| `make -B -C sdk/examples/systest -j4` | `sdk-build.txt` | Fresh compile and link of i686 and x86_64 systest, including all SDK libc sources. |
| `sh ports/tcc/build.sh build/test/sdk-allocator-tcc` | `tcc-build.txt` | Fresh i386/x86_64 SDK archives; archive members and exactly one definition of each allocator entry point checked with `ar`/`nm`. |
| `sh ports/ffmpeg/build.sh i686 build/test/sdk-allocator-ffmpeg-i686 audio` | `ffmpeg-i686.txt`, `ffmpeg-i686-configure.txt` | Fresh configure-libc archive, configure/link probes and all five requested FFmpeg audio-profile libraries built. |
| `sh ports/ffmpeg/build.sh x86_64 build/test/sdk-allocator-ffmpeg-x86_64 audio` | `ffmpeg-x86_64.txt`, `ffmpeg-x86_64-configure.txt` | Same fresh integration checks for x86_64, including assembly. |
| `make -j4` | `iso-build.txt` | Both kernels, SDK examples, bundled applications and bootable ISO built. Existing compiler warnings are retained in the log. |

No dependency version or license was changed. The existing pinned TinyCC and
FFmpeg source trees were used. FFmpeg's **audio** profile was rebuilt from fresh
output directories; this is not a claim of a fresh full-codec-profile build.

## Banana execution

Each run used the newly built ISO and QEMU's software emulation, with e1000
user-mode networking. The `qemu-*.txt` files contain the runner results;
corresponding `qemu-*-serial.txt` files contain complete normalized serial output.

| Guest | Command file | Result |
| --- | --- | --- |
| x86_64 BIOS, 512 MiB, 4 CPUs | `commands-512m.txt` | Four checks passed; unavailable physical high RAM skipped by selftest; an explicit high-RAM request correctly rejected; two heap lifecycle runs passed. |
| x86_64 UEFI, 512 MiB, 4 CPUs | `commands-512m.txt` | Same four checks passed with OVMF. |
| x86_64 BIOS, 8 GiB, 4 CPUs | `commands-8g.txt` | Four checks passed; all sixteen kernel-owned 4 MiB pieces were above 4 GiB; selftest measured 0 MiB high RAM in use before and after process exit; two heap lifecycle runs passed. |
| i386 BIOS, 512 MiB, 1 CPU | `commands-512m.txt` | Four checks passed using the existing kernel-allocation fallback; no local-heap support is claimed for i386. |

All eight `systest heap` executions exercised real Banana pthread startup,
destructors that allocate/free, normal return, `pthread_exit`, aligned realloc
preservation, kernel-owned realloc/free, and the actual BMP decoder's returned
buffer. Each reported zero live/reserved heap bytes at completion. The test
checks that `pthread_cancel` returns ENOSYS; cancellation is not implemented.

The negative high-RAM test intentionally emits `physical high-RAM check failed`
and returns 3. Its expected diagnostic makes the serial runner's check pass.
This is the regression guard: SDK heap virtual addresses above 4 GiB must never
turn a low-physical-memory guest into a high-RAM success.

Replay the BIOS 512 MiB run:

```sh
python3 tools/serialtest.py --timeout 240 \
  --log build/logs/allocator-bios-512m.log \
  --cmds-file docs/validation/sdk-allocator/commands-512m.txt -- \
  -cdrom Banana_OS.iso -m 512M -smp 4 -nic user,model=e1000
```

For the 8 GiB run, use `-m 8G`, `commands-8g.txt` and a different log path.
For i386, add `--qemu qemu-system-i386` before `--`, omit `-smp 4`, and use a
different log path. For UEFI, first copy the firmware variables into the worktree:

```sh
cp /usr/share/OVMF/OVMF_VARS_4M.fd build/test/sdk-allocator-OVMF_VARS.fd
python3 tools/serialtest.py --timeout 240 \
  --log build/logs/allocator-uefi-512m.log \
  --cmds-file docs/validation/sdk-allocator/commands-512m.txt -- \
  -cdrom Banana_OS.iso -m 512M -smp 4 -nic user,model=e1000 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=build/test/sdk-allocator-OVMF_VARS.fd
```

## Limits and remaining work

This retry fixes the high-RAM false positive and supplies missing execution
evidence for the bounded allocator adaptation. It does not complete
`base-18-sdk-heap-evaluation`: forced pressure/swap, comprehensive process-teardown
leak accounting, real HTTP-buffer ownership, and baseline/adapted syscall counts,
throughput, resident memory and desktop latency remain unmeasured. The high-RAM
exit check measures kernel umem reclamation, not all process mapping overhead.
No physical hardware test was run. The existing follow-up and base-18 history
remain unchanged and require independent review and controller validation.

Antigravity supplied a bounded high-memory correction proposal. Its allocation
approach was checked against `kernel/umem.c` and adapted locally. Its suggested
kernel/initrd boot commands were unsuitable for this tree; the actual runs above
used the existing ISO/serial-test path. No delegated execution claim was used as
test evidence.
