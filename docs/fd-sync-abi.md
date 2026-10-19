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
