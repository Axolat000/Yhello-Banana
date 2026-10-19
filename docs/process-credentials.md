# Process identity policy (base-03, step 1)

SDK applications now have kernel-owned effective UID, effective GID and up to
16 supplementary groups in `app_proc_t.credentials`. The primary group is the
effective GID, separate from the supplementary list. Credentials are initialized
before the app becomes runnable and remain immutable for its lifetime. No SDK
call can supply, change or obtain a pointer to them.

## Launch and service boundary

- `app_exec` and `app_spawn` share `prepare`: a launch from a native kernel
  context starts as banana (UID 1000, GID 1000, no supplementary groups).
  Native shell, desktop and daemon launchers never implicitly confer root.
- A launch made on behalf of an existing app copies all of its credentials by
  value. Launching from an app worker thread follows the same rule. There is
  no setuid executable support or privileged SDK app launcher.
- All threads resolve the same app record through the existing task-to-app
  mapping. There is no mutable global current-user variable; scheduling among
  tasks cannot transfer identity. Existing thread/process cleanup clears this
  mapping before a task ID is reused.
- `app_current_credentials()` returns a kernel-only const view of the current
  app credentials, including while a syscall executes in the kernel. For a
  native kernel task with no app mapping, it returns administrator UID/GID 0.
  `app_credentials_in_group()` checks the primary and supplementary groups.

The administrator context is reserved for trusted native kernel code, including
boot, drivers and system services. It is not an authentication result: login
currently unlocks the single-user desktop and does not select an account.
Filesystem owner constants remain root=0 and banana=1000.

## Scope and compatibility

This step supplies identity for subsequent authorization work. It does **not**
yet deny file access: base-03 steps 2 and 3 must use these credentials for path
traversal, file operations, ownership changes and other privileged operations.
The existing SDK getuid/geteuid/getgid/getegid wrappers still return the fixed
single-user identity; a future identity-query ABI is needed before exposing
additional user identities to SDK apps. There is no SDK table, disk format,
license or durability contract change in this step.

Built-in shell commands/scripts and native network services bypass SDK app
contexts. They currently remain trusted administrator code; assigning and
propagating native user-session credentials requires separate work. Browser
isolation remains covered by base-04. None of this makes the 32-bit ring-0 app
execution path a security boundary: an app there can modify kernel memory.
The 64-bit ring-3 path is required for meaningful isolation.

## Validation

Run from the repository root:

```
python3 tools/test_app_credentials.py
python3 tools/test_app_credentials.py --sanitize
python3 tools/test_app_credentials.py --32
make -j2 kernel/app.o kernel/app.o64
make
python3 tools/serialtest.py --cmd 'selftest ring3 ## 0 failed' --cmd 'selftest cores ## 0 failed' --cmd 'pkg install ~/Examples/threads.bpk' --cmd 'threads 4 200000' --log build/logs/base-03-targeted-bios.log -- -cdrom Banana_OS.iso -m 512 -smp 4 -nic none
python3 tools/serialtest.py --cmd 'selftest ring3 ## 0 failed' --cmd 'selftest cores ## 0 failed' --cmd 'pkg install ~/Examples/threads.bpk' --cmd 'threads 4 200000' --log build/logs/base-03-targeted-uefi.log -- -cdrom Banana_OS.iso -m 512 -smp 4 -nic none -bios /usr/share/ovmf/OVMF.fd
```

The host tests include the real `kernel/app.c` translation unit and use linker
section collection to discard the unused OS runtime. Only the current task ID
is stubbed. They exercise launch credential selection and task mapping with two
identities, primary/supplementary membership, inheritance by value, worker
threads and task-ID reuse. They do not execute ELF loading, app entry, scheduling
or filesystem authorization. The production kernel builds check both architecture
paths; QEMU app tests cover existing launch/thread behavior, not protected-file
denial. The file-mode and symlink denial tests belong to the remaining substeps.

Execution evidence on the Debian worker (2026-10-19):

- `python3 tools/test_app_credentials.py`: passed; log
  `build/logs/base-03-host.log`.
- `python3 tools/test_app_credentials.py --sanitize`: passed with AddressSanitizer
  and UndefinedBehaviorSanitizer; log `build/logs/base-03-sanitize.log`.
- `make -j2 kernel/app.o kernel/app.o64`: both objects compiled without warnings;
  log `build/logs/base-03-compile.log`.
- `python3 tools/test_app_credentials.py --32`: compiled, but host execution
  terminated with `SIGSYS` (signal 31); not a runtime pass. Log:
  `build/logs/base-03-host32.log`.

- Full ISO build through `bash tools/dev.sh apps bios`: succeeded in 706 seconds
  with 151 warnings outside the changed credential code. The networked QEMU
  stage reached the shell, but DHCP output after the prompt prevented the
  harness from recognizing readiness; that stage was interrupted, not passed.
  Logs: `build/logs/base-03-apps-bios-driver.log`, `build/logs/apps-bios.log`.
- Broad default selftests with `-nic none` on BIOS and UEFI were interrupted
  after socket tests failed/stalled. These are not successful full-suite runs.
  Logs: `build/logs/base-03-offline-bios.log` and
  `build/logs/base-03-offline-uefi.log`.
- The targeted QEMU commands above passed on both BIOS and UEFI (exit 0):
  ring-3 selftest (1 passed, 0 failed), multicore selftest (2 passed, 0 failed),
  thread package installation and `threads 4 200000`. Serial logs:
  `build/logs/base-03-targeted-bios.log` and
  `build/logs/base-03-targeted-uefi.log`; command results are in the matching
  `base-03-targeted-*-driver.log` files. Networking was disabled to avoid the
  asynchronous DHCP/prompt issue; no network validation is claimed.

No physical hardware validation was performed. Logs and build artifacts are
retained locally and are not included in the source commit.
