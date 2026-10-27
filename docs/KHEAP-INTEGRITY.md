# Kernel heap integrity adaptation

This change adapts the diagnostic and real-code testing ideas from pinned
upstream commit `a4b2e406812dbd2973736a3a8b444588624a2845`. The fork baseline
inspected was `316de84a09f0b56f2eedb06a54283769caffc70a`.

## Scope and invariants

The fork already has 32 free-block bins, `find_fit()`, bin-based aligned
allocation and a bin-based largest-free query. Those allocation paths are
unchanged. Upstream's TLSF replacement and timings against unbinned parent
`d9c9071` are omitted: rounded searches can skip an exact 528-byte block, and
that benchmark does not compare against the fork. No whole upstream branch
was merged. There are no new dependencies, licensing changes, SDK ABI changes,
disk-format changes or durability-contract changes.

`kheap_check(err, cap)` is an explicit read-only diagnostic. It returns zero on
success and one on the first detected error. It clears the error buffer on
success; a NULL buffer or nonpositive capacity is allowed. It checks:

- Header and payload alignment, magic, address overflow and physical back links.
- Contiguous tiling within each region and no adjacent free blocks.
- Used zero-size lead/tail sentinels for appended regions. Region transitions
  may move downward in address space: built-in package storage returned by
  `kernel/builtin_apps.c` can be below the original heap.
- Used-byte accounting and total capacity, including split-header overhead.
  `g_total` excludes the original body header of each region and its sentinels.
- Free-bin pointers against the validated physical list before dereferencing
  them, then their classes, back links and exact membership counts.

Physical traversal has a capacity-derived node bound; bin traversal has the
validated free-block count as its bound. Both stop at the first error. No
production fault-injection function or unchecked free-block-count traversal
is exported. The host test includes a copy of the actual allocator in one
translation unit to reach its private state.

This is a diagnostic, not recovery from arbitrary memory corruption. Physical
headers/next pointers must still point to readable memory. There is no separate
trusted region registry: coordinated corruption of headers/accounting, invalid
region registration or inaccessible physical pointers is outside its guarantee.
It allocates no scratch memory and does not modify allocator metadata. Bin
pointer validation costs O(B * F) for B physical blocks and F free blocks, under
the heap lock with interrupts disabled. Invoke it explicitly, not in production
allocation paths or latency-sensitive periodic services. No speedup is claimed.

## Tests and reproduction

Run `python3 tools/test_kheap.py`. The host suite builds both pointer widths
where a libc toolchain exists, runs a separate runtime probe, and reports
unavailable execution as SKIP. Each process is bounded by a 120-second timeout.
The test checks allocations, zeroing, frees, in-place/moving/failed realloc,
alignment through 64 KiB, exhausted exact fits around size-class boundaries
(including 528), and 120000 deterministic mixed operations with every retained
byte checked. A single mapped arena guarantees added regions lie below and
above the initial heap; touching regions must remain separate on exhaustion
and full reclamation. Fault cases include physical/bin cycles, unknown bin
pointers, missing/duplicate entries, wrong classes, back links, sentinel damage,
and used/total accounting mismatches. Host locks are stand-ins; this is not an
SMP test.

`selftest heap` checks integrity, then 4000 aligned allocation/realloc rounds
among 256 retained blocks, checking data and structure throughout. There is no
host-dependent timing threshold. `selftest cores` also runs a temporary
cooperative heap checker while the existing app threads allocate; the checker
ends with the workload. It reports the number of checks actually executed.

For QEMU, build with `make -j4 SB_KEY=build/test/kheap/no-signing-key` (an absent
path, producing unsigned test loaders). Create a disposable 64 MiB FAT32 image
with `truncate -s 64M build/test/kheap/cache-bios.img` and
`/usr/sbin/mkfs.fat -F 32 -n HEAPTEST build/test/kheap/cache-bios.img`. Then run:

```sh
python3 tools/serialtest.py --log build/logs/kheap-bios-serial.log \
  --cmds-file tools/tests/kheap-qemu.cmds -- \
  -cdrom Banana_OS.iso -m 512M -smp 4 -nic user,model=e1000 \
  -drive file=build/test/kheap/cache-bios.img,format=raw,if=virtio -boot d
```

Use a separate freshly formatted image for UEFI and append
`-bios /usr/share/ovmf/OVMF.fd`. The command file runs heap and multicore tests,
filesystem create/append/remount/read-back tests, and heap checks with populated
file caches and after `drop_caches`. These cache checks are between filesystem
operations, not instrumentation inside filesystem critical sections. The serial
driver requests one fresh prompt if asynchronous boot notifications obscure a
prompt it already observed; it never sends that newline before shell startup.

For the 32-bit kernel, use `--qemu qemu-system-i386` and
`--cmd 'selftest heap ## 0 failed'` with `-cdrom Banana_OS.iso -m 512M -nic none`.

## Execution evidence

Results and exact log paths for this candidate are recorded below. QEMU results
are virtual-machine evidence, not physical-hardware validation.

Observed on 2026-10-22:

| Check | Result | Log |
| --- | --- | --- |
| `make -j4 SB_KEY=build/test/kheap/no-signing-key` | Both kernels and BIOS/UEFI ISO built; unrelated USB initializer warnings remain | `build/logs/kheap-build.log` |
| `python3 tools/test_kheap.py` | 64-bit suite passed; 32-bit suite compiled, execution skipped because its separate runtime probe received SIGSYS (-31) | `build/logs/kheap-host.log`, `build/test/kheap/{64,32}/{build,probe-run}.log`, `build/test/kheap/64/run.log` |
| 64-bit host suite with AddressSanitizer and UndefinedBehaviorSanitizer | Passed, no sanitizer findings | `build/logs/kheap-sanitized-build.log`, `build/logs/kheap-sanitized.log` |
| BIOS QEMU, 512 MiB, four cores, FAT32 virtio disk | All seven command checks passed; filesystem test 14 passed/0 failed; 16 integrity checks during each multicore workload | `build/logs/kheap-bios.log`, `build/logs/kheap-bios-serial.log` |
| UEFI QEMU, same memory/cores/disk type | All seven command checks passed; filesystem test 14 passed/0 failed; 24 and 15 integrity checks during the multicore workloads | `build/logs/kheap-uefi.log`, `build/logs/kheap-uefi-serial.log` |
| 32-bit BIOS QEMU, 512 MiB | `selftest heap`: 3 passed/0 failed | `build/logs/kheap-32.log`, `build/logs/kheap-32-serial.log` |

Both filesystem runs populated file caches; `drop_caches` subsequently released
512 KiB of file data and the heap checks passed on both sides of eviction.
These FAT32 runs do not validate ext4's block cache or durability. The initial
BIOS/UEFI attempts timed out waiting for a prompt obscured by boot notifications;
those logs are retained as `build/logs/kheap-{bios,uefi}-prompt-timeout*.log`.
The successful runs above include the serial-driver correction.

The sanitized binary was built with:

```sh
cc -m64 -std=gnu11 -O1 -g -Wall -Wextra -Werror -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I build/test/kheap/64 tools/tests/kheap.c \
  -Wl,--defsym=_kernel_end=0x100000 -o build/test/kheap/64/test-sanitized
ASAN_OPTIONS=detect_leaks=1 timeout 120 build/test/kheap/64/test-sanitized
```

The P2 `base-kheap-integrity` checklist entry is a planning proposal preserving
existing entries and execution history. Independent review and controller
validation must reconcile it with this adaptation before scheduling. Native
32-bit host execution and physical hardware were not validated here.
