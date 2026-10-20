# File synchronization ABI (version 22)

`banana_api_t.fd_sync(int fd)` is appended after `utc_offset`, preserving all
version 21 field offsets. The kernel advertises version 22 and the enlarged
`size`. Existing applications keep their table layout. New callers must check
`BANANA_HAS(api, fd_sync)` before reading/calling the pointer on an older kernel;
an unavailable entry must be treated as unsupported, never as successful sync.
There is no disk-format change.

The entry returns zero only for completed synchronization, or a negative errno
directly (it does not set the application's SDK `errno`):

| Result | Meaning |
| --- | --- |
| `-9` (`EBADF`) | No current application, negative/out-of-range/unused/closed descriptor, or missing/deleted file entry. |
| `-22` (`EINVAL`) | Terminal descriptor 0–2 or an open socket cannot be synchronized by this call. |
| `-38` (`ENOSYS`) | An open regular file is valid, but its synchronization backend is not connected yet. |
| `-5` (`EIO`) | Reserved for a storage failure when the backend is connected. |

Validation uses the calling application's existing descriptor lookup. Read-only
file descriptors are accepted for validation. The call does not move the file
position or close the descriptor. On x86_64 the integer argument is registered
with the syscall dispatcher and the call stays on the boot core; on i386 the
ABI table points directly to the same implementation.

This is base-02 step 1 only. It intentionally provides no successful storage
path yet. Step 2 must connect the appropriate volume's cache/journal/device flush
and propagate failures; step 3 must replace the SDK's existing no-op `fsync` and
`fdatasync` wrappers and define directory synchronization. Currently `a_open`
rejects directories, so directory support remains necessary in that later step.
No durability guarantee is established by this ABI addition or its host tests.

One backend hazard to resolve in step 2: `fsdisk_sync()` returns `FSDISK_OK`
while `g_frozen` defers writes for backups. A successful application sync cannot
inherit that early return without waiting for real synchronization or returning
an error. A proposed acceptance test is recorded in the living checklist.

Run `python3 tools/test_fd_sync.py --sanitize` and
`python3 tools/test_fd_sync.py --32` for host execution of the actual kernel
entry, lookup and close functions with task/filesystem/socket stubs. The tests
cover invalid and closed descriptors, the last descriptor, deleted/missing file
entries, terminals, sockets, read-only files, unchanged position, unsupported
backends, ABI size checks and x86_64 syscall argument registration. They do not
execute ring-3 entry, real storage, QEMU power cuts or physical hardware.
Compile both kernel paths with `make kernel/app.o kernel/app.o64`.

## Execution evidence for this change

On the Debian worker on 2026-10-19:

- `python3 tools/test_fd_sync.py --sanitize`: passed on x86_64 with
  AddressSanitizer and UndefinedBehaviorSanitizer. Log:
  `build/logs/base-02-fd-sync-host.log`.
- `python3 tools/test_fd_sync.py --32`: the i386 harness compiled, but its
  process was terminated by `SIGSYS` at execution. Its runtime checks are
  unverified. Log: `build/logs/base-02-fd-sync-host32.log`.
- `make kernel/app.o kernel/app.o64`: both freestanding kernel objects compiled
  successfully, with no compiler diagnostics. Log:
  `build/logs/base-02-app-build.log`.

Logs are local ignored build artifacts. No complete OS build, QEMU run, power-cut
experiment or physical hardware test was performed for this substep.

## Checkpoint resumption verification

The resumed worktree contains checkpoint
`90497854722f7a1b9dfa940639905a3aaa8e7819` on top of integration base
`fe73f98e2bf9aef877b4fdea5f8f73a724d6fd7c`. Source inspection confirmed:

- `fd_sync` remains the final table member after the v21 `utc_offset` entry;
  `api_init()` publishes its pointer, version 22 and the complete table size.
- `spec_init()` registers exactly one integer argument. There is no local-core
  override for this slot, preserving the existing boot-core dispatch default.
- `a_fd_sync()` resolves the calling process before looking up its descriptor,
  rejects terminals and sockets, and uses `get_fd()` for range, allocation and
  live file-entry validation. Its only regular-file result is still `ENOSYS`.
- The frozen-backup checklist proposal matches the current early-success return
  in `fsdisk_sync()`; it remains a proposal for independent review and controller
  validation, not evidence that backend integration is finished.

The execution logs listed above were inspected and retained. No source change
to the ABI implementation or test harness was needed during resumption, so
successful compilation and host checks were not repeated. The prior i386
runtime failure remains unresolved; compilation alone does not validate that
runtime. The six checkpoint source paths and this verification update form the
step 1 review candidate. Backend synchronization and SDK wrapper work remain
separate, unfinished substeps.

A bounded Antigravity review was checked against the source during resumption.
No suggested implementation patch was accepted: `SPEC` uses member offsets,
so registration order does not allocate slots; `APP_FD_MAX` is 253, making 255
the correct last descriptor; and position preservation and boot-core dispatch
are already asserted. The suggested host-test macro has no implementation and
would not establish why i386 execution received `SIGSYS`. That limitation remains
reported rather than bypassed.

## Step 2 Backend Wiring and Volume Semantics

In step 2, `a_fd_sync` connects the ABI entry to actual disk synchronization
while handling distinct volume types and backup freeze states:

1. **Installed Disk Files (`mnt == 0`, `fsdisk_is_installed() == 1`)**:
   - Synchronized via `fsdisk_sync_wait()`, which triggers `ext4_flush()` and the underlying disk controller/device flush (`disk_flush()`).
   - If the filesystem is currently frozen for a backup (`g_frozen`), `fsdisk_sync_wait()` waits up to 10 minutes (matching the freeze duration in `e4_write`) for the hypervisor backup to thaw and completes the write and flush. If the freeze times out, it returns `FSDISK_ERR_IO`.
   - On success (`FSDISK_OK`), `a_fd_sync()` returns `0`.
   - On storage or controller failure (`FSDISK_ERR_IO`), `a_fd_sync()` returns `-5` (`EIO`).

2. **RAM Disk Files (`mnt == 0`, `fsdisk_is_installed() == 0`)**:
   - In a live CD / memory session without an installed disk, files exist entirely in volatile RAM.
   - Calling sync on these files fails fail-closed with `-38` (`ENOSYS`), preventing false durability promises to applications.

3. **External Mounted Volumes (`mnt > 0`)**:
   - Files residing on other mount points (e.g. FAT32 or exFAT USB drives) do not participate in `fsdisk_sync`.
   - `a_fd_sync` returns `-38` (`ENOSYS`) until filesystem-specific sync handlers are wired for those volumes.

Host tests in `tools/tests/fd_sync.c` verify this volume distinction, mock freeze timeouts, error propagation, and descriptor safety under ASan and UBSan.

## Step 3 SDK Wiring and Directory Synchronization

In step 3, the SDK POSIX wrappers and directory synchronization semantics are completed:

1. **POSIX `fsync` and `fdatasync` in `sdk/lib/unix.c`**:
   - `fsync(int fd)` checks `BANANA_HAS(__banana, fd_sync)` and invokes `__banana->fd_sync(fd)`.
   - On error (return value `< 0`), the negative error code is converted directly to `errno = -rc` and returns `-1`. This propagates `EBADF` (9), `EINVAL` (22), `ENOSYS` (38), and `EIO` (5) to callers.
   - On success (return value `0`), `fsync` returns `0`.
   - If invoked against an older kernel where `fd_sync` is unavailable (`!BANANA_HAS`), it fails fail-closed with `errno = ENOSYS` and returns `-1`.
   - `fdatasync(int fd)` delegates directly to `fsync(fd)`.

2. **Directory Synchronization Semantics**:
   - POSIX allows opening directory descriptors using `open(dir, O_RDONLY)` and calling `fsync(dirfd)` to ensure directory entry modifications (e.g. `mkdir`, `rename`, `unlink`) are committed to durable media.
   - In Banana OS kernel architecture (`kernel/app.c`), `a_open` rejects opening directory paths with `-1` (`fs_find_dir(path) >= 0`), and `DIR*` handles returned by `opendir()` use internal heap tracking (`struct __dir`), with `dirfd(DIR* d)` returning `-1` (`ENOTSUP`).
   - For installed ext4 filesystems, directory mutations (`fs_mkdir`, `fs_move`, `fs_delete`) are written through the ext4 journal (JBD2). Synchronizing any file descriptor via `a_fd_sync()` or `fsync()` commits pending transactions via `ext4_flush()`.
   - In accordance with POSIX, any attempt to call `fsync()` or `fdatasync()` on an invalid or directory descriptor that cannot be opened returns `EBADF`. When full directory descriptors are introduced in future ABI extensions, directory `fd_sync` will route through the same journal commit path.
