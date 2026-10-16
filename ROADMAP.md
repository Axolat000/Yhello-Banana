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
| ext2/ext4 and NTFS (read) | planned | To open Linux and Windows disks. |
| Crash-safe disk filesystem | planned | A journal, so a power cut while saving can't damage the disk. |
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
| Compiled Tailwind CSS v3 | **testing** (build 151) | Offline Tailwind 3.4.17 fixtures: escaped arbitrary/fraction/responsive utilities, `rem` spacing, flex layout and resize. Improved computed custom properties with cycle/fallback handling, dimensional `calc()`/`min()`/`max()`/`clamp()`, escaped selectors and `:is()`/`:where()` specificity. Host checks pass; VM confirmation pending. This is a tested subset, not full Tailwind compatibility. |
| Tailwind CSS v4 | planned | Also `@layer`, `@property`, `oklch()`, `color-mix()`. |
| Runtime stylesheet and DOM observer foundations | **testing** (build 151) | Inserted, replaced and removed `<style>` tags; linked-source cache; actual asynchronous MutationObserver records, filters and old values, with bounded feedback/overflow. `selftest browser` checks these offline. The Tailwind CDN compiler itself remains unverified. |
| React, Vue, Alpine | **testing** (build 151) | Real pinned React 18.3.1, Vue 3.5.13 and Alpine 3.14.9 mount counters and update on clicks in host tests. Fixed template/regex parsing, AsyncFunction construction, Proxy ownership, `with` scope and external `defer` ordering. `selftest browser`; visual demos at `about:frameworks`, `/vue`, `/alpine`. VM verification and broader applications remain pending. |
| Astro | **testing** (build 151) | Real Astro 5.13.2 production output: static HTML without JavaScript, scoped CSS, a module script, and independent Preact `client:load`, `client:idle` and `client:only` islands. Fixed absolute module specifiers with URL schemes and added `Node.isSameNode()` for slots/templates. `selftest browser`; demo at `about:frameworks/astro`. VM confirmation, other renderers/directives, view transitions and server islands remain pending. |
| Real-site browser target: SuperWebManager | **testing** (build 151) | https://swm.mxtpasontop.fr/ serves Astro 7.3.5 and Tailwind 4.3.3. Host: all 16 checks pass, including desktop grid, readable heading, hydration/interactions and four decoded images. Fixed Window globals, explicit grid columns, final font-relative lengths and WebP decoding (libwebp 1.6.0, scalar decoder). Retina screenshots fit the bounded eight-million-pixel browser limit; document arena uses about 109 MiB. `selftest browser swm` uses the real browser transport/decoder; host `tools/test_browser.py --swm` exports a preview. User confirmed VM networking/scripts; latest layout/image fixes await VM confirmation. Full Tailwind v4 and visual parity remain incomplete. |

Grid regression coverage includes fixed/percentage/fractional columns, `minmax`, numeric `repeat`, gaps, automatic rows and resize (up to 16 columns). Spans, named lines, auto-fit/auto-fill and explicit row tracks are not implemented. Static lossy/lossless WebP and alpha streams decode; animated WebP is unsupported.

## Not planned

Section 3 of the original list (a CI boot test, untracking build outputs, README fixes) was left out on purpose. Two parts of it were done anyway because the build needed them: the stale tracked `.o` files are no longer tracked, and `tools/dev.sh` boots and tests the system.
