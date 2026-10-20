# Testing Banana OS

Banana OS checks itself in two places: **commands inside Banana OS** that test a
feature where it runs (a real PC, a VM), and **tools on the Linux build host**
that build the ISO, boot it in QEMU and drive those commands over the serial
console. Most of them came with pull request #30 (builds 141-150); `fsck` and
`vmtools` were added later (builds 151-152).

Every test prints one line per check - `ok`, `FAIL` (with the reason) or
`skip` (with why it cannot run here) - and a summary with the number failed.

## DHCP state inventory

See [DHCP state and failure matrix](DHCP-STATE.md) for the current timers,
Settings/ifconfig diagnostics, known lease/carrier gaps, host characterization
commands and the controlled-network qualification procedure. Execution evidence
for base-17 step 1 is in [DHCP validation](DHCP-STATE-VALIDATION.md).

## DNS and socket state characterization

See [DNS and SDK socket mapping state matrix](DNS-SOCKET-STATE.md) for the
characterization of RFC 1035 A/AAAA DNS queries, error and timeout handling,
caching, NXDOMAIN, truncated responses (TC bit), and SDK POSIX getaddrinfo /
socket family mappings. Host characterization tests run via:
```bash
python3 tools/test_dns_socket_state.py --sanitize
```

## Monotonic Clock and UTC Time Synchronization

See [Monotonic Clock and UTC Time Synchronization](CLOCK-MONOTONIC-UTC.md) for
the architectural separation of monotonic time (`timer_ms()`) and real time
(`rtc_now_utc()`), time correction models (step vs slew), proof of TCP delay and
deadline invariance under wall-clock jumps, and NTP/SNTP protocol specifications.
Host characterization tests run via:
```bash
python3 tools/test_clock_monotonic_utc.py --sanitize
```


## Inside Banana OS

### `selftest [part]`

Checks the system's foundations, from user mode to every processor core. What
has to run as an app is done by `systest` (`sdk/examples/systest`), installed
from the copy built into the kernel; each of its modes' exit code is the result.

```
selftest                 ring3, memory, limits, cores, OpenCL, offline browser,
                         POSIX and IPv6 (if enabled) checks
selftest ring3           an app runs in user mode (64-bit kernel; skipped on 32-bit)
selftest memory          RAM above 4 GiB: an app gets 64 MiB from there, every byte
                         holds, and it is given back when the app ends
                         (skipped without RAM above 4 GiB: MEM=8G tools/dev.sh run)
selftest limits          an app with 60 threads, an app with 60 files open,
                         24 apps at once
selftest cores           threads calling malloc / free / a lock on every core, and
                         whether they spread over the cores (SMP=4 in QEMU)
selftest opencl          installs and runs the cltest example: build errors in the
                         log, vadd, a 2D Mandelbrot range, atomics, math built-ins,
                         __local arguments, barrier(), local arrays, float4 / int /
                         float8-16 vectors, refused group sizes
selftest boot            render the shared BIOS/UEFI menu into a guarded buffer;
                         highlight, countdown, clipping, color, stride and fallback checks;
                         incremental updates preserve unchanged options and header pixels
selftest ram             firmware RAM reporting: complete maps before/after legacy counts,
                         8/24/64 GiB totals, reserved holes, malformed input and live totals
selftest mouse           deterministic relative-movement, ownership and release-state checks
selftest mouse demo      also launch the interactive mouse demo (desktop must be running);
                         click to capture, look around, Escape releases, Q exits
selftest browser         offline framework, CSS/layout, DOM observer and WebP tests
selftest browser swm     live SuperWebManager HTTPS, layout, hydration and image tests
selftest posix [part]    installs and runs the posixtest example: the POSIX layer for
                         ported programs - mmap: a 1 GiB reservation, mprotect, holes
                         mapped again, file mappings, system calls on mapped buffers,
                         threads faulting pages in on every core (posix mmap); pthreads:
                         mutexes, condition variables, __thread (64-bit), keys, once,
                         150 detached threads, a 4 MiB stack, semaphores, barriers,
                         read-write locks, errno per thread, clocks (posix threads)
                         sockets: TCP echo of 100 KB and UDP on the loopback, poll,
                         select, FIONREAD, SO_RCVTIMEO, ECONNREFUSED, getaddrinfo,
                         inet_pton/ntop, and an HTTP request to example.com if the
                         network reaches it (posix sockets); epoll and eventfd over
                         TCP sockets (one-shot, a wakeup from another thread), pipes,
                         socket pairs, dup2 over a socket (posix epoll)
                         signals (64-bit): SIGSEGV / SIGFPE / SIGILL from faults with
                         siginfo, a handler changing RIP/RAX, PROT_READ enforced, masks,
                         pthread_kill to a busy and a sleeping thread, sigaltstack, a
                         stack overflow caught (posix signals)
                         dlopen: a library needing another (DT_NEEDED), dlsym,
                         constructors, calls into the C library and back into the app,
                         dladdr, dl_iterate_phdr, dlerror, dlclose (posix dlopen)
selftest ipv6 [host [port]]
                         IPv6 off blocks TCP, UDP and resolution; address parsing and
                         formatting; localhost; bracketed HTTP URLs; UDP loopback
                         (odd lengths, bad checksums and truncation dropped, extension
                         headers, fragments); TCP connect / listen / accept, 8193
                         bytes both ways, orderly close; IPv4 unaffected. With a host
                         (and port, 80 by default): an AAAA lookup and a TCP
                         connection over the real network. IPv6 is switched on for
                         the test and put back as it was; saved settings are untouched.
```

Expected: `selftest: N passed, 0 failed`.

### `fstest [-k] <mount point>`

Checks a mounted FAT32 or exFAT drive end to end, in a folder of its own on it
(`fstest/`): make a folder, write a small file and append to it, write 300 KiB
and append 100 KiB, rename (to a Unicode name on exFAT), make a sub-folder and
move a file into it, 300 files so the folder outgrows its first cluster, delete
half of them, then **unmount and mount again** and read everything back from the
drive itself - the renamed file, every byte of the 400 KiB file, the 150 files
left and none of the deleted ones. Then the test folder is removed; `-k` keeps it to look at.

```
fstest /mnt/usb          a USB stick
fstest -k /mnt/virtio    a virtio disk, keeping /mnt/virtio/fstest
```

Works on real SD cards and sticks as well as in QEMU. `lsblk` and `mount` list
the mount points.

### `crashtest [mode]`

The kernel crash screen (stop code, registers, faulting address, stack words).

```
crashtest                (= preview) shows the screen safely; Enter or Esc returns
crashtest report         prints a sample report in the terminal
crashtest panic          a real kernel panic          - HALTS the system
crashtest ud2            an invalid-opcode exception  - HALTS the system
crashtest pagefault      a read of page zero (64-bit) - HALTS the system
```

Save your work before the last three; the full report also goes to COM1.

### `toggle6` / `ipv6`

Not tests themselves, but `selftest ipv6` uses them: `toggle6 [on|off]` (or
`ipv6 on|off`, Settings > Network) switches IPv6; `ipv6` shows the addresses.

### Later additions

- `fsck [-f] [-y]` (build 151): the filesystem tree, both metadata copies on the
  installed disk and the extents; `-f` reads every file back against its CRC-32C.
- `vmtools` (build 152): which hypervisor, and which guest tools answer.
- `help --check` (build 153): every shell command has a help page.

## On the build host (Linux)

Run these from the repository root. They need `nasm`, `gcc-multilib`,
`xorriso`, `mtools`, `python3` and `qemu-system-x86` (Debian:
`sudo apt install nasm gcc-multilib binutils xorriso mtools python3 qemu-system-x86 ovmf`).

### `tools/dev.sh`

```
tools/dev.sh build                    build the ISO; log in build/logs/build.log
tools/dev.sh run  [bios|uefi|32]      build, then boot in a QEMU window; the serial
                                      console in the terminal and build/logs/serial-<fw>.log
tools/dev.sh test [bios|uefi|32] [s]  build, boot headless for s seconds (60), fail on a PANIC
tools/dev.sh apps [bios|uefi|32]      build, boot headless and run selftest and the
                                      threads / cltest examples over the serial console
tools/dev.sh clean                    make clean
```

Environment: `MEM` (QEMU memory, default `512M`), `SMP` (cores, default `4`),
`NIC` (default `e1000`), `OVMF` (UEFI firmware file), `DISK` (an image attached
as a virtio disk, mounted at `/mnt/virtio`). For example `MEM=8G tools/dev.sh apps`
exercises the RAM above 4 GiB.

### `tools/serialtest.py`

Boots Banana OS in QEMU without a window and types commands at its serial
console, each once the prompt is back. A command fails if its output shows a
panic or a crashed app, or - written `command ## text` - lacks `text`.

```
tools/serialtest.py [--qemu BIN] [--log FILE] [--timeout S] [--boot-timeout S]
                    [--cmd "..."]... [--cmds-file FILE] [--window] [--keep] -- <qemu args>
```

Without `--cmd` / `--cmds-file` it runs the default app tests: `selftest`,
installs and runs `threads 4 200000` and `cltest`, then `free`. In a commands
file: one command a line, `#wait N` waits N seconds, other `#` lines are
comments. `--window` shows QEMU's screen, `--keep` leaves it running afterwards.
Exit code 0 when every command passed.

### CI firmware smoke jobs

The Build ISO workflow runs separate `boot` matrix jobs for BIOS and x86_64
UEFI on Ubuntu 24.04. Both download the same `boot-iso-<SHA>` artifact from
the build job rather than rebuilding it. Reproduce them from the repository root:

```
make
bash tools/ci-boot.sh bios
bash tools/ci-boot.sh uefi
```

The boot-only dependencies are `python3`, `qemu-system-x86` and `ovmf` (plus
Git and `sha256sum` for provenance). The build dependencies are listed above.
UEFI uses `/usr/share/OVMF/OVMF_CODE_4M.fd` and a private copy of
`/usr/share/OVMF/OVMF_VARS_4M.fd`; override `OVMF_CODE` and `OVMF_VARS` together
for another matching firmware pair. Secure Boot is disabled in this test.
QEMU uses software emulation, four cores, 512 MiB RAM and no network device.
The firmware selects the default 64-bit kernel from the ISO.

Each job must reach a serial shell prompt, then run `selftest boot` and
`selftest ram` with a `0 failed` result, within 180 seconds for boot and
180 seconds per command. A timeout, missing expected result, crash or panic
reported during a command fails the job. The job itself has a 15-minute limit.
These are offline boot smoke checks, not the full app/POSIX, filesystem,
installation, reboot, update or hardware test suites.

`build/logs/boot-<firmware>/` contains `serial.log`, `results.log` and
`provenance.txt` (commit SHA, ISO SHA-256, QEMU version and arguments).
CI attempts to upload these even on failure as
`boot-<firmware>-<SHA>-<attempt>` with 14-day retention; failures before QEMU
starts can have no serial log. The ISO artifact is retained for three days.
Firmware variable copies are disposable and are not uploaded.

This implements base-07 step 1 only. Existing pre-release publication is
unchanged and does not wait for the boot jobs. The current workflow has no
stable channel; stable gating and its intentional-failure demonstration remain
base-07 step 3. Do not treat successful smoke jobs as full release validation.

### `tools/test-exfat.sh [bios|uefi]`

The exFAT driver end to end (needs `exfatprogs`): makes an empty 64 MiB exFAT
image, boots Banana OS with it as a virtio disk and runs `mount`, `df` and
`fstest -k /mnt/virtio`; then, on Linux, `fsck.exfat` must find the volume clean
and `tools/exfat_check.py` checks what Banana OS left there - the tree, 150 files
in `fstest/many`, the Unicode-named file, and the 400 KiB file byte for byte.
`HEADLESS=1` runs without a window, `KEEP=1` keeps QEMU open first.

### `tools/exfat_check.py`

An exFAT reader of its own (not Banana OS's driver), used by test-exfat.sh:

```
tools/exfat_check.py IMAGE list              every file and folder, with sizes
tools/exfat_check.py IMAGE cat PATH          a file's contents
tools/exfat_check.py IMAGE cmp PATH LOCAL    exit 0 if the file equals LOCAL
```

### `tools/test_opencl.sh [x86_64|i686]`

The OpenCL runtime and its `cltest` checks, built and run on the host with the
same TinyCC vector patch and `setjmp` as Banana OS, once with 1 and once with 4
worker threads. Run `ports/tcc/build.sh` first (it prepares the TinyCC sources).

### `tools/test_crashscreen.py`

The crash screen's real formatter and renderer (`kernel/crash_draw.c`), compiled
for the host with `tools/tests/crashscreen.c`: 32- and 64-bit reports, fault
flags, long messages, clipped and padded framebuffers (nothing drawn outside
them). It writes preview PNGs and reports to `build/test/crashscreen/` without
booting anything; prints `PASS` when every check holds.

## A full check before a release

```
tools/dev.sh test bios && tools/dev.sh test uefi && tools/dev.sh test 32
tools/dev.sh apps
MEM=8G tools/dev.sh apps
tools/test-exfat.sh
tools/test_opencl.sh x86_64 && tools/test_opencl.sh i686
python3 tools/test_crashscreen.py
```

Then inside Banana OS: `selftest`, `selftest ipv6 example.com`, `fsck -f` on an
installed disk, `fstest` on a real stick, and `crashtest` for the screen.

### Browser host checks

`python3 tools/test_browser.py` runs the offline suite; add `--32` for i686,
`--sanitize` for ASan/UBSan, or `--swm` for the live website checks and a PNG
preview. The live host checks need libcurl development files and pkg-config.
The OS commands use Banana OS networking; the host transport uses libcurl.
See [the fixture documentation](../web/fixtures/README.md) for coverage and limits.

### Mouse capture host checks

`python3 tools/test_mouse_capture.py` tests the real app-window event and capture
implementation, the SDK wrappers on older API tables, and shared input math.
Use `--32` for the i686 ABI or `--sanitize` for ASan/UBSan. This uses host
framebuffer/heap stubs; it does not validate physical devices or hypervisor I/O.

### Boot menu host checks

`python3 tools/test_bootmenu.py` runs the same renderer checks as `selftest boot`
and exports 640x480, 1024x768 and 1920x1080 PNG previews under `build/test/boot/`.
Use `--32` or `--sanitize` for the i686 ABI or ASan/UBSan. These renderer tests
need neither QEMU nor Pillow. `tools/build_boot_font.py` optionally regenerates
the committed atlas from `fonts/DejaVuSans.ttf` (Pillow required; license in
`fonts/LICENSE`). Normal OS builds consume the atlas directly.

Firmware smoke coverage for build 168: an isolated ISO booted under SeaBIOS and
64-bit OVMF, detected the graphical menu, paused the timer, moved selection with
the arrow keys and started the kernel with Enter. The 32-bit UEFI loader was
compiled, linked and checked for relocations and valid PE conversion; it was not
run under 32-bit UEFI firmware. Rebuild with `tools/dev.sh run` to see the menu;
`tools/dev.sh run uefi` selects the UEFI path.

Build 170 adds 22 incremental redraw checks (37 total). Selection, display
and countdown changes repaint only affected bands, composed in a 3 KiB
scanline buffer before copying to the framebuffer. Repeated unchanged input
writes no pixels. Tests compare incremental output with a full render at
640x480 and 1024x768 and check cache invalidation when the framebuffer or
menu layout changes. The BIOS linker still enforces the loader's 64 KiB
address-space limit.

The updated renderer passed ASan/UBSan and i686 host checks, both kernel
links, BIOS/UEFI32/UEFI64 loader builds, BIOS/UEFI64 QEMU menu navigation
and kernel handoff, and all 37 `selftest boot` checks in a 64-bit BIOS VM.

### RAM reporting

`python3 tools/test_bootmem.py --sanitize` (or `--32`) runs the same 16
memory-map regression checks as `selftest ram`. The in-OS command also checks
that the detected total covers the RAM managed by the allocators. Rebuild and
boot with `MEM=8G tools/dev.sh run`, then run `selftest ram` and `neofetch`;
Settings > About reads the same total. These are usable firmware RAM totals:
reserved memory and the PCI hole are excluded. A 32-bit kernel can report RAM
above 4 GiB even though it cannot allocate from it.

Build 169 validation: all 17 in-OS checks passed with 8 GiB assigned under
64-bit BIOS, 64-bit UEFI and 32-bit BIOS. Neofetch reported 8191 MB under
BIOS and 8185 MB under UEFI. The host checks also cover synthetic 24/64 GiB
maps; those larger guest sizes were not run on this host.
