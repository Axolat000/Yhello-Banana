# Banana OS roadmap

The features planned after build 138, and how far each one is.

Status: **done** (committed and tested) · **testing** (written, not yet confirmed in a VM) · **next** · **planned**

Run `selftest` inside Banana OS for the foundations, or select `selftest ring3`,
`selftest memory`, `selftest limits`, or `selftest cores`. The latest VM run
passed all nine checks with RAM above 4 GiB and four CPU cores. Run
`fstest <mount point>` for FAT32/exFAT; `-k` keeps the test files afterwards.
New features should include a test command that runs inside Banana OS.

RAM reporting (build 169): Settings and neofetch prefer the complete firmware
memory map over the legacy count below the PCI hole. `selftest ram` checks
above-4-GiB totals, reserved regions, tag ordering and live accounting.

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
| App manager in Settings | **done** (build 160) | Settings > Apps: the installed apps with version, size, running or update available; Open, Stop, Update (in a background task), Uninstall (confirmed by a second click). Also the launcher's Details and `settings apps [name]`. Tested in QEMU on 64- and 32-bit: open, stop, uninstall. Update not yet tried against a newer package. |
| Install profiles | **done** (build 159) | Full / Standard / Minimal on a new page of the installer and `install --standard` / `--minimal`; the built-in apps and examples are the `builtin:` package source (from the kernel, no network), so whatever is left out installs from the App Store. Tested in QEMU (minimal install, two boots, apt install from builtin:). |
| A real window manager | **done** (build 157) | One stacking order for all windows (terminals, built-in apps, installed apps as a layer), focus to the next window down, Aero-style snapping (drag to edges/corners with a preview, Win+arrows), title-bar window menu, Alt+drag, always on top, 4 virtual desktops (taskbar switcher, Win+Ctrl+arrows, move windows between them), shadows. Checked in QEMU with screenshots. |
| Installer: choose the disk | **done** (build 156) | The graphical installer lists every disk with what is on it and installs on (or updates) the one picked; `install N` / `update N` in the shell. Tested in QEMU: blank IDE + SATA + virtio data disks, installed on IDE, booted, data disks intact. |
| Every drive in Files | **done** (build 155) | The navigation pane lists every mounted volume (label + folder, USB or disk icon), scrolls when they do not fit; Eject and Properties (free space) work on any drive. Checked in QEMU with a SATA disk (3 partitions), a GPT virtio disk (2) and a USB stick. |
| Several disks | **done** (build 154) | The installed disk is found among several (it used to need exactly one); extra IDE/SATA disks become drives; every partition (MBR primary + logical, GPT) is mounted; 16 drives / 16 mounts; `lsblk`, `mount <drive>[pN] [dir]`, `umount <drive>`. Tested in QEMU on the live CD and beside an installed system. |
| Help with a page per command | **done** (build 153) | `help`, `help <command>` / `man` / `--help`, `help <topic>`, `help -k` / `apropos`, `help -a` (paged), did-you-mean for unknown commands, `help --check` (every shell command has a page). Checked in QEMU. |
| Guest tools for VMs | **done** (build 152) | Hypervisor detection; the QEMU guest agent over a new virtio-serial driver (Proxmox: IPs, clean shutdown/reboot, set-time, fsfreeze for backups); VMMouse (VMware, QEMU vmport); VMware TCLO/RPCI (power operations, guest IP/OS); VirtualBox VMMDev (mouse integration, host clock). `vmtools`. Tested in QEMU TCG with Proxmox's agent setup: 18 agent commands, shutdown through the agent, VMMouse placing the pointer. VMware's channel and VirtualBox's VMMDev not yet tried on those hypervisors. |
| Crash-safe disk filesystem | **done** (layout 5; checksums build 151) | Saves are copy-on-write (new extents, then the metadata into the other of two slots), so a power cut leaves the previous save whole - what a journal gives ext4. Build 151 seals both with CRC-32C. |
| POSIX layer for OpenJDK 17 | **done** (builds 162-166) | For a port of OpenJDK 17 (then OpenGL 3.3, for Minecraft "in theory"). **mmap / munmap / mprotect: done** (build 162): reserved addresses, pages on first touch, protection on 64-bit; `selftest posix`. **pthreads: done** (build 163): stacks of any size, detached threads, mutexes, condition variables, rwlocks, keys, once, barriers, semaphores, `__thread` (64-bit), per-thread `errno`, clocks, `sysconf`. **BSD sockets: done** (build 164): TCP / UDP over IPv4 as file descriptors, poll / select, getaddrinfo; no IPv6 sockets yet. **Signals: done** (build 165): sigaction, masks, kill / raise / pthread_kill, sigaltstack, sigsetjmp; on 64-bit CPU faults become SIGSEGV / SIGFPE / SIGILL with a modifiable ucontext. **dlopen: done** (build 166): ELF shared libraries with DT_NEEDED, constructors, the app's symbols. `selftest posix`: 60 checks on 64-bit (71 with epoll, build 172). Next: the OpenJDK 17 port itself (Zero), then OpenGL 3.3. |
| OpenJDK 17 (64-bit) | **started** (build 171) | `ports/openjdk/build.sh`: HotSpot and java.base's native libraries rebuilt against the SDK, a java.base image, the `java` app. `java -version` and Hello World run in QEMU. **Networking: done** (build 172): epoll and eventfd (and pipes, socket pairs) made by the SDK, so NIO selectors, sockets and `HttpURLConnection` work; `NetTest` passes. Build 174: `jdk.unsupported` in the image, `java -jar` (libzip linked to libjava), LWJGL runs. **JIT: done** (build 175): HotSpot's server VM (template interpreter, C1 and C2) replaces Zero; `JitTest` (exceptions from compiled code, stack overflow, deoptimization, GC safepoints) passes, `Bench` ~120x faster than the interpreter. Next: other processes (`ProcessBuilder`), more modules (java.desktop needs AWT). |
| OpenGL 3.3 | **done** (build 173) | Core profile, GLSL 3.30: Mesa 24.3's softpipe (on the processor) as `libOSMesa.so`, built by `ports/mesa/build.sh`; `<GL/gl3.h>` and `USE_GL3 = 1` for apps, windows or offscreen; `gl3test` passes in QEMU. 64-bit apps only. Next: faster rendering (llvmpipe needs LLVM; a GPU driver for virtio-gpu / Intel would be the real speed-up). |
| LWJGL 3 for Java | **done** (build 174) | LWJGL 3.3.3's official jars (core, OpenGL, GLFW) with native libraries for Banana OS from `ports/lwjgl/build.sh`: `liblwjgl.so` (+ libffi), `liblwjgl_opengl.so`, `libglfw.so` (GLFW 3.4's API over `bwin` and `bgl3`), `libGL.so.1` (`glXGetProcAddress`). The `LwjglTriangle` test draws in a window in QEMU (13 fps). Needs `-Dos.name=Linux`. Not there: GLFW joysticks, gamma, fullscreen, window position; the other LWJGL modules (OpenAL, stb, FreeType, tinyfd, jemalloc). Next for Minecraft: those modules, more JDK modules, speed. |
| ZIP archives | **done** (build 176) | `zip` / `unzip` in the shell and Extract / Compress in Files, on any drive: deflate (`kernel/deflate.c`, the kernel's own compressor) and stb's inflater, CRC-32 checked, `../` and absolute names refused. Archives made by Banana OS pass Linux's `unzip -t` and Python's `zipfile`; Info-ZIP's extract unchanged. `selftest zip`: 7 checks. Not there: ZIP64 (over 4 GB / 65535 entries), encryption, other formats (tar, gzip, 7z). |
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
| Graphical firmware boot menu | **done** (build 168) | Shared BIOS/UEFI renderer, compact antialiased DejaVu atlas, Banana yellow focus, countdown progress and readable keyboard/display hints. All existing kernel and firmware actions remain. Text fallback for unsupported framebuffers. `selftest boot` checks clipping, stride, color layouts, small screens and highlights. Host sanitizer/i686 tests and BIOS/UEFI64 QEMU menu/navigation/handoff tests pass; both UEFI builds pass relocation/PE checks. No UEFI32 runtime test performed. |
| Mouse capture for native games | **done** (build 167) | SDK API 20: foreground window ownership, raw relative input, coalesced deltas, three buttons and wheel; Escape/focus/minimize/desktop/exit release. VMMouse requests relative reports while captured; VirtualBox absolute integration pauses. Absolute-only tablets fall back to position differences and remain limited by host coordinates. `selftest mouse`; `selftest mouse demo` opens an interactive look-around window. Host ownership, lifecycle, event-queue and old-SDK compatibility checks pass; user confirmed in-OS tests and the interactive demo in a VM. Physical hardware remains untested. |
| Desktop notifications | planned | |
| Several user accounts on the lock screen | planned | |
| Screenshots | planned | A key and a `screenshot` command. |

## Browser: Tailwind and other frameworks

| Feature | Status | Notes |
|---|---|---|
| Compiled Tailwind CSS v3 | **testing** (build 166) | Offline Tailwind 3.4.17 fixtures: escaped arbitrary/fraction/responsive utilities, `rem` spacing, flex layout and resize. Improved computed custom properties with cycle/fallback handling, dimensional `calc()`/`min()`/`max()`/`clamp()`, escaped selectors and `:is()`/`:where()` specificity. Host checks pass; VM confirmation pending. This is a tested subset, not full Tailwind compatibility. |
| Tailwind CSS v4 | planned | Also `@layer`, `@property`, `oklch()`, `color-mix()`. |
| Runtime stylesheet and DOM observer foundations | **testing** (build 166) | Inserted, replaced and removed `<style>` tags; linked-source cache; actual asynchronous MutationObserver records, filters and old values, with bounded feedback/overflow. `selftest browser` checks these offline. The Tailwind CDN compiler itself remains unverified. |
| React, Vue, Alpine | **testing** (build 166) | Real pinned React 18.3.1, Vue 3.5.13 and Alpine 3.14.9 mount counters and update on clicks in host tests. Fixed template/regex parsing, AsyncFunction construction, Proxy ownership, `with` scope and external `defer` ordering. `selftest browser`; visual demos at `about:frameworks`, `/vue`, `/alpine`. VM verification and broader applications remain pending. |
| Astro | **testing** (build 166) | Real Astro 5.13.2 production output: static HTML without JavaScript, scoped CSS, a module script, and independent Preact `client:load`, `client:idle` and `client:only` islands. Fixed absolute module specifiers with URL schemes and added `Node.isSameNode()` for slots/templates. `selftest browser`; demo at `about:frameworks/astro`. VM confirmation, other renderers/directives, view transitions and server islands remain pending. |
| Real-site browser target: SuperWebManager | **testing** (build 166) | https://swm.mxtpasontop.fr/ serves Astro 7.3.5 and Tailwind 4.3.3. Host: all 16 checks pass, including desktop grid, readable heading, hydration/interactions and four decoded images. Fixed Window globals, explicit grid columns, final font-relative lengths and WebP decoding (libwebp 1.6.0, scalar decoder). Retina screenshots fit the bounded eight-million-pixel browser limit; document arena uses about 109 MiB. `selftest browser swm` uses the real browser transport/decoder; host `tools/test_browser.py --swm` exports a preview. User confirmed VM networking/scripts; latest layout/image fixes await VM confirmation. Full Tailwind v4 and visual parity remain incomplete. |

Grid regression coverage includes fixed/percentage/fractional columns, `minmax`, numeric `repeat`, gaps, automatic rows and resize (up to 16 columns). Spans, named lines, auto-fit/auto-fill and explicit row tracks are not implemented. Static lossy/lossless WebP and alpha streams decode; animated WebP is unsupported.

## Not planned

Section 3 of the original list (a CI boot test, untracking build outputs, README fixes) was left out on purpose. Two parts of it were done anyway because the build needed them: the stale tracked `.o` files are no longer tracked, and `tools/dev.sh` boots and tests the system.
