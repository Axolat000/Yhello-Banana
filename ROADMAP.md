# Banana OS roadmap

The features planned after build 138, and how far each one is.

Status: **done** (committed and tested) · **testing** (written, not yet confirmed in a VM) · **next** · **planned**

Run `selftest` inside Banana OS for the foundations, or select `selftest ring3`,
`selftest memory`, `selftest limits`, or `selftest cores`. The latest VM run
passed all nine checks with RAM above 4 GiB and four CPU cores. Run
`fstest <mount point>` for FAT32/exFAT; `-k` keeps the test files afterwards.
New features should include a test command that runs inside Banana OS.

## 1. Foundations

| Feature | Status | Notes |
|---|---|---|
| Apps in ring 3, isolated from the kernel | **done** (build 141) | 64-bit kernel: each app has its own page tables, `int 0x80` system calls, and its pointers are checked. Already-built apps work unchanged. The 32-bit kernel keeps apps in ring 0. |
| Memory above 4 GiB | **done** (build 142) | Apps' memory comes from RAM above 4 GiB first, up to 512 GiB. `free` showed 23 GB in a friend's VM. |
| Higher process limits | **done** (build 143) | 32 apps (was 8), 64 threads per app (16), 64 open files (16), 32 app windows (6), 32 web views (8), 256 tasks (48). |
| System calls on every core | **done** (build 144) | Calls that only touch the app's own state (`malloc`/`free`/`realloc`, `wake`, `wait` when nothing needs waiting for, clock, thread ids, text drawing) run on the core that made them instead of moving back to core 0. The heap, the RAM above 4 GiB, app page tables and each app's memory have locks for it. A fully multi-core kernel (locks in the file system, network and GUI) is a much bigger rewrite and isn't planned. |

## 2. Missing features

| Feature | Status | Notes |
|---|---|---|
| Wi-Fi | planned | A USB Wi-Fi dongle (Realtek RTL8188/8192) on the existing USB stack first, with WPA2. Intel cards (`iwlwifi`-class) need firmware files and are much bigger. |
| Sleep (ACPI S3) | planned | Builds on the ACPI code used for power-off: suspend, wake through real mode, devices restored. |
| IPv6 for TCP and UDP | **done** (build 148) | Optional, off by default; Settings / `toggle6 [on\|off]`. TCP/UDP, AAAA DNS, IPv6 HTTP URLs and `curl` / `wget -6`. User's VM passed 13 local checks, then all 16 checks in `selftest ipv6 example.com 80`: SLAAC, AAAA resolution, an IPv6 TCP connection to port 80, and restoration to off. Host packet tests also passed. HTTP response bodies and HTTPS over IPv6 have not yet been verified in the VM. |
| exFAT | **done** (build 145; test improvements in build 147) | Read and write. The user confirmed `fstest` passes, including folder growth, deleting, remounting and checking file contents. Earlier host checks found a clean volume and an identical binary copy. Run `fstest /mnt/virtio` (or your drive's mount point). |
| ext4-class native filesystem | **done** (build 151) | 255-byte names, 511-byte paths, Unix modes/owners/groups (checked with the owner's bits), symbolic links, atime with relatime, CRC-32C on the metadata and every file written, 1M files / 256K folders, files up to 2 GiB; metadata version 7. `fsck [-f] [-y]`, `stat`, `chmod`, `chown`, `chgrp`, `ln -s`, `readlink`. Tested in QEMU: made on the live CD, installed, booted from the disk - names, modes, owners, links and data came back, `fsck -f` clean (59 files read back). A disk installed by build 150, then `update`d, booted with its files intact, moved to version 7 on its first save, `fsck -f` clean. Both kernels (64- and 32-bit) pass the same checks. No hard links. |
| ext2/ext4 and NTFS (read) | planned | To open Linux and Windows disks. |
| Crash-safe disk filesystem | **done** (layout 5; checksums build 151) | Saves are copy-on-write (new extents, then the metadata into the other of two slots), so a power cut leaves the previous save whole - what a journal gives ext4. Build 151 seals both with CRC-32C. |
| `fork`, `exec` and `pipe` | planned | Ring-3 isolation (build 141) makes these possible. They unlock porting Unix tools and pipelines between programs. |

## 4. Smaller wins

| Feature | Status | Notes |
|---|---|---|
| Browser: scrolling inside iframes | planned | |
| Browser: links inside iframes | planned | |
| Browser: WebSocket client | planned | Uses the existing TLS. |
| OpenCL: `barrier()` and vector types (`float4`) | **done** (build 149) | CPU work-group barriers, shared local arrays/arguments, float/int/uint vectors with 2/3/4/8/16 lanes. User's four-core VM passed every check in `selftest opencl`, including reductions, private state across barriers, vector operations and error handling (0 failed; 894 ms). Host kernel tests also pass on x86_64 and i686 with one and four workers. See SDK notes for subset and limits. |
| Verbose kernel crash screen | **done** (build 150) | Windows-style blue header with Linux-style diagnostics: stop code, CPU/task, registers, fault decoding and raw stack words; full serial report. `crashtest` previews safely; `crashtest report` prints a sample. Explicit panic/ud2/pagefault modes halt the OS. User approved the screen; host formatting/rendering checks and both kernel builds/links pass. Individual fatal-test modes have not been separately confirmed. |
| Browser: recover from a crashed page | **testing** | Merged upstream commit `793a736` locally in `558da3e`, retaining ring-3 apps and dynamic task stacks. Open `about:crash` to check that the page closes without stopping the OS. Both architectures compile; VM verification pending. |
| Desktop notifications | planned | |
| Several user accounts on the lock screen | planned | |
| Screenshots | planned | A key and a `screenshot` command. |

## Browser: Tailwind and other frameworks

| Feature | Status | Notes |
|---|---|---|
| Tailwind CSS v3 | planned | CSS variables with `var()` fallbacks, `calc()`, `:is()`/`:where()`, escaped class names (`.md\:flex`). |
| Tailwind CSS v4 | planned | Also `@layer`, `@property`, `oklch()`, `color-mix()`. |
| Tailwind's script build (`cdn.tailwindcss.com`) | planned | Needs `MutationObserver` and generated `<style>` tags. |
| React, Vue, Alpine | planned | Mostly modern JavaScript: classes, async/await, Promises, Proxy, modules. Each one gets tested against real pages. |

## Not planned

Section 3 of the original list (a CI boot test, untracking build outputs, README fixes) was left out on purpose. Two parts of it were done anyway because the build needed them: the stale tracked `.o` files are no longer tracked, and `tools/dev.sh` boots and tests the system.
