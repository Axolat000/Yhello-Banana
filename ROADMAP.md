# Banana OS roadmap

The features planned after build 138, and how far each one is.

Status: **done** (committed and tested) · **testing** (written, not yet confirmed in a VM) · **next** · **planned**

## 1. Foundations

| Feature | Status | Notes |
|---|---|---|
| Apps in ring 3, isolated from the kernel | **done** (build 139) | 64-bit kernel: each app has its own page tables, `int 0x80` system calls, and its pointers are checked. Already-built apps work unchanged. The 32-bit kernel keeps apps in ring 0. |
| Memory above 4 GiB | **done** (build 140) | Apps' memory comes from RAM above 4 GiB first, up to 512 GiB. `free` showed 23 GB in a friend's VM. |
| Higher process limits | **done** (build 141) | 32 apps (was 8), 64 threads per app (16), 64 open files (16), 32 app windows (6), 32 web views (8), 256 tasks (48). |
| System calls on every core | **next** | Calls that only touch the app's own state (`malloc`/`free`, `wait`/`wake`, clock, thread ids, text drawing) run on the core that made them instead of moving back to core 0. A fully multi-core kernel (locks in the file system, network and GUI) is a much bigger rewrite and isn't planned. |

## 2. Missing features

| Feature | Status | Notes |
|---|---|---|
| Wi-Fi | planned | A USB Wi-Fi dongle (Realtek RTL8188/8192) on the existing USB stack first, with WPA2. Intel cards (`iwlwifi`-class) need firmware files and are much bigger. |
| Sleep (ACPI S3) | planned | Builds on the ACPI code used for power-off: suspend, wake through real mode, devices restored. |
| IPv6 for TCP and UDP | planned | IPv6 currently stops at `ping6`. |
| exFAT | planned | The default on SD cards over 32 GB and many USB sticks. |
| ext2/ext4 and NTFS (read) | planned | To open Linux and Windows disks. |
| Crash-safe disk filesystem | planned | A journal, so a power cut while saving can't damage the disk. |
| `fork`, `exec` and `pipe` | planned | Ring-3 isolation (build 139) makes these possible. They unlock porting Unix tools and pipelines between programs. |

## 4. Smaller wins

| Feature | Status | Notes |
|---|---|---|
| Browser: scrolling inside iframes | planned | |
| Browser: links inside iframes | planned | |
| Browser: WebSocket client | planned | Uses the existing TLS. |
| OpenCL: `barrier()` and vector types (`float4`) | planned | |
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
