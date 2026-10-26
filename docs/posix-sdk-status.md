# POSIX SDK Function Status and False Success Audit

This document establishes the official status matrix and inventory of POSIX/Linux library
functions provided by Banana OS (`sdk/lib/unix.c`, `sdk/lib/libc.c`, `sdk/lib/mman.c`, `sdk/lib/ufd.c`).
It serves as the specification and architectural baseline for Task **base-05** ("Corriger les faux succès POSIX et les verrous").

## Purpose and Scope

Banana OS ports diverse Linux/Unix software (OpenJDK, Mesa, LWJGL, FFmpeg, SDL2, mbedtls).
Historically, several POSIX APIs were implemented as simple stubs that unconditionally return 0 (success)
or mimic success without performing any backing kernel or filesystem operation. Such false successes
trick ported applications and runtime libraries into assuming security guarantees (permissions),
consistency guarantees (flock locks, sync durability), or concurrency guarantees (pread/pwrite cursor isolation)
that do not actually exist on the system.

This inventory categorizes each function into one of four distinct contract states:
1. **Implemented**: Backed by genuine kernel, ABI, or userspace logic fulfilling the POSIX contract.
2. **Partial Implementation**: Works partially or for basic cases, but carries significant divergences, missing checks, or concurrency hazards.
3. **Fake Success (Constant 0 / Advisory Stub)**: Unconditionally returns 0 without enforcing the requested effect or checking validity.
4. **Unsupported (ENOSYS / ENOTSUP / Fail-Closed)**: Fails explicitly and cleanly with appropriate errno.

---

## Contract and Status Matrix

### 1. File Locking and Concurrency

| Function | Source | Current Status | Observable Behavior & Risks | Planned Target (base-05 Steps 2 & 3) |
|---|---|---|---|---|
| `flock(int fd, int op)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Always returns `0` regardless of `fd` validity or `op` flags (`LOCK_SH`, `LOCK_EX`, `LOCK_NB`, `LOCK_UN`). Callers believe they hold exclusive lock while concurrent processes/threads write concurrently. | Step 2: Implement kernel file identity locking table supporting `LOCK_SH`, `LOCK_EX`, `LOCK_NB`, `LOCK_UN`; release on close/death; return `EWOULDBLOCK`/`EAGAIN`. |
| `pread(int fd, void* buf, size_t n, off_t off)` | `sdk/lib/unix.c` | **Partial Implementation** | Saves file offset with `lseek(SEEK_CUR)`, moves to `off`, reads, and restores original offset. **Critical race condition**: concurrent readers/writers on the same descriptor or fork/dup alter each other's offsets during the non-atomic seek-read-seek sequence. | Step 3: Implement atomic offset-based read syscall / descriptor position isolation so the shared file offset is never altered. |
| `pwrite(int fd, const void* buf, size_t n, off_t off)` | `sdk/lib/unix.c` | **Partial Implementation** | Saves file offset with `lseek(SEEK_CUR)`, moves to `off`, writes, and restores original offset. Same severe race condition as `pread`. | Step 3: Implement atomic offset-based write without moving or corrupting shared descriptor position. |
| `ftruncate(int fd, off_t len)` | `sdk/lib/unix.c` | **Partial Implementation** | Zero-extends files by writing zero chunks; fails with `ENOTSUP` on shrink (`len < end`). Alters file seek position and restores it. | Keep explicit error on shrink until filesystem truncation ABI is added; avoid seek cursor disruption. |
| `ftrylockfile(FILE* f)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns 0 immediately; no file locking across threads. | Evaluate stdio lock tracking or document single-thread-per-stream model. |

### 2. Permissions, Identity, and Metadata

| Function | Source | Current Status | Observable Behavior & Risks | Planned Target (base-05 Step 3) |
|---|---|---|---|---|
| `chmod(const char* path, mode_t mode)` | `sdk/lib/unix.c` | **Partial Implementation** | Calls `access(path, 0)`. Only checks that the file exists; completely ignores `mode`. Callers believe file permissions changed to read-only or executable, but filesystem metadata is untouched. | Step 3: Connect to filesystem inode metadata modification or fail explicitly if underlying filesystem is read-only or unsupported. |
| `fchmod(int fd, mode_t mode)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0` immediately without validating `fd` or touching file mode. | Step 3: Connect to descriptor's underlying inode mode modification; validate descriptor validity (`EBADF`). |
| `chown(const char* path, uid_t u, gid_t g)` | `sdk/lib/unix.c` | **Partial Implementation** | Calls `access(path, 0)`. Ignores UID/GID. | Document single-user root/banana OS architecture; validate path exists. |
| `fchown(int fd, uid_t u, gid_t g)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0` without validating `fd` or setting owner/group. | Validate descriptor (`EBADF`); return 0 only for valid descriptors if single-user model is retained. |
| `lchown(const char* path, uid_t u, gid_t g)` | `sdk/lib/unix.c` | **Partial Implementation** | Alias to `chown(path, u, g)`. | Follow `chown`. |
| `access(const char* path, int mode)` | `sdk/lib/unix.c` | **Partial Implementation** | Checks `__banana->stat(path, &st) == 0`. Ignores `mode` (`R_OK`, `W_OK`, `X_OK`). Any existing file is considered readable, writable, executable. | Check file type and mode flags against requested access mode. |
| `umask(mode_t m)` | `sdk/lib/unix.c` | **Implemented** (In-memory) | Stores static mask and returns previous mask. | Valid userspace process state. |

### 3. Storage Flushing and Durability

| Function | Source | Current Status | Observable Behavior & Risks | Alignment with base-02 |
|---|---|---|---|---|
| `fsync(int fd)` | `sdk/lib/unix.c` | **Implemented** (Fail-Closed) | Connected in Task base-02 Step 3 to `__banana->fd_sync(fd)`. Returns `0` on durable sync, or negative errno (`EBADF`, `EINVAL`, `ENOSYS`, `EIO`). | Preserves strict fail-closed contract. |
| `fdatasync(int fd)` | `sdk/lib/unix.c` | **Implemented** (Fail-Closed) | Delegates directly to `fsync(fd)`. | Aligned with `fsync`. |
| `sync(void)` | `sdk/lib/unix.c` | **Fake Success** (Empty Stub) | POSIX fire-and-forget advisory flush across filesystems. Performs no action. | Document advisory contract or wire global ext4/ramfs flush when available. |
| `msync(void* addr, size_t len, int flags)` | `sdk/lib/mman.c` | **Fake Success** (Constant 0) | Returns `0` immediately without flushing dirty mapped memory pages back to file backing store. | When dirty mmap writeback is implemented, wire to volume sync; otherwise document memory-backed model. |

### 4. Process and Thread Scheduling / Controls

| Function | Source | Current Status | Observable Behavior & Risks | Contract Assessment |
|---|---|---|---|---|
| `setpgid(pid_t a, pid_t b)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Ignores parameters, returns `0`. | Banana OS has no process groups; ports assume process grouped. |
| `alarm(unsigned s)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0`, schedules no SIGALRM. | Timers must not rely on `alarm()`; software may hang waiting for SIGALRM. |
| `setrlimit(int res, const struct rlimit* r)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Unconditionally returns `0`. Resource limits (file size, stack, memory) are ignored. | Expected by runtimes like OpenJDK. |
| `getpriority(int which, id_t who)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0`. | Priority manipulation is advisory in Banana OS scheduler. |
| `setpriority(int which, id_t who, int prio)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0`. | Scheduler does not support per-app POSIX nice levels. |
| `prctl(int option, ...)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0`. Ignores thread name and memory naming options. | Harmless for naming, but potential issue for capability options. |
| `posix_spawnattr_*` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0` on attribute init/destroy. | Note that `posix_spawn` itself cleanly fails with `ENOSYS`. |
| `posix_spawn_file_actions_*` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0` on action init/destroy. | Staged actions fail cleanly when passed to `posix_spawn` (`ENOSYS`). |

### 5. Memory Management and Advisories

| Function | Source | Current Status | Observable Behavior & Risks | Contract Assessment |
|---|---|---|---|---|
| `madvise(void* addr, size_t len, int advice)` | `sdk/lib/mman.c` | **Fake Success** (Constant 0) | Returns `0`. `MADV_DONTNEED` does not purge pages. | Permissible under POSIX advisory semantics, but applications must not rely on memory reclaim. |
| `posix_madvise(void* addr, size_t len, int advice)`| `sdk/lib/mman.c` | **Fake Success** (Constant 0) | Returns `0`. Advisory. | Standard POSIX advisory behavior. |
| `mlock(const void* addr, size_t len)` | `sdk/lib/mman.c` | **Fake Success** (Constant 0) | Returns `0`. Banana OS paging does not swap to disk, so memory is physically resident by definition. | Functionally safe because pages cannot be paged out to swap. |
| `munlock(const void* addr, size_t len)` | `sdk/lib/mman.c` | **Fake Success** (Constant 0) | Returns `0`. | Functionally safe. |
| `mincore(void* addr, size_t len, unsigned char* vec)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Sets all page status vector bytes to `1` (resident) and returns `0`. | Accurate given that all allocated virtual memory pages are resident in RAM. |
| `malloc_usable_size(void* p)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0`. | Runtimes calling this may miscalculate pool buffer sizes. |
| `malloc_trim(size_t pad)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0`. | Advisory heap return. |

### 6. Extended Attributes, Timestamps, and Odds & Ends

| Function | Source | Current Status | Observable Behavior & Risks | Contract Assessment |
|---|---|---|---|---|
| `futimes(int fd, const struct timeval t[2])` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0` without validating `fd` or modifying inode timestamps. | Filesystem has no user-settable timestamp ABI. |
| `posix_fadvise(int fd, off_t off, off_t len, int advice)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0`. | POSIX advisory call; harmless no-op. |
| `listxattr(const char* p, char* l, size_t s)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0` (empty attribute list). | Consistent with xattr manipulation functions returning `ENOTSUP`. |
| `flistxattr(int fd, char* l, size_t s)` | `sdk/lib/unix.c` | **Fake Success** (Constant 0) | Returns `0` (empty attribute list). | Consistent with xattr functions returning `ENOTSUP`. |

### 7. Explicitly Unsupported APIs (ENOSYS / ENOTSUP / EPERM / ECHILD)

| Function | Source | Error Code | Reason & System Limitation |
|---|---|---|---|
| `fork()`, `vfork()` | `sdk/lib/unix.c` | `ENOSYS` | Multi-process fork not implemented; apps run in cooperative/separate containers. |
| `execve()`, `execv()`, `execvp()` | `sdk/lib/unix.c` | `ENOSYS` | Process replacement not implemented. |
| `posix_spawn()` | `sdk/lib/unix.c` | `ENOSYS` | Process spawning via POSIX spawn not connected to kernel app launcher. |
| `wait()`, `waitpid()`, `waitid()`, `pclose()` | `sdk/lib/unix.c` | `ECHILD` | No child processes spawned through POSIX process model. |
| `shmget()`, `shmat()` | `sdk/lib/unix.c` | `ENOSYS` | System V IPC shared memory not available. |
| `inotify_init()`, `inotify_init1()` | `sdk/lib/unix.c` | `ENOSYS` | File change notifications not implemented; allows Java/tool fallbacks. |
| `memfd_create()` | `sdk/lib/unix.c` | `ENOSYS` | Anonymous memory file descriptors not implemented. |
| `popen()` | `sdk/lib/unix.c` | `ENOSYS` | Subprocess pipe execution not supported. |
| `dirfd()` | `sdk/lib/unix.c` | `ENOTSUP` | Directory streams are heap objects (`struct __dir`), not kernel file descriptors. |
| `fdopendir()` | `sdk/lib/unix.c` | `ENOTSUP` | Cannot construct directory stream from raw descriptor. |
| `fchdir()` | `sdk/lib/unix.c` | `ENOTSUP` | Changing working directory via file descriptor not supported. |
| `mkfifo()` | `sdk/lib/unix.c` | `ENOTSUP` | Named FIFOs not supported. |
| `symlink()`, `link()`, `mknod()` | `sdk/lib/unix.c` | `EPERM` | Hard links, symbolic links, and device nodes forbidden on filesystem. |
| `getxattr()`, `fgetxattr()`, `setxattr()`, `fsetxattr()`, `removexattr()`, `fremovexattr()` | `sdk/lib/unix.c` | `ENOTSUP` | Extended attributes not supported by filesystem drivers. |

---

## Detailed Focus for base-05 Substeps

### Flock and File Locking (Step 2 Focus)
- `flock` in `sdk/lib/unix.c` currently does:
  ```c
  int flock(int fd, int op) { (void)fd; (void)op; return 0; }
  ```
- This is completely ineffective. When two apps open `/tmp/lockfile`, both acquire the lock concurrently.
- Step 2 requires introducing a common file identity (e.g. device + inode number or canonical path lookup in `kernel/fs.c` / `kernel/app.c`), tracking shared locks (`LOCK_SH`) and exclusive locks (`LOCK_EX`), supporting non-blocking attempts (`LOCK_NB` returning `EWOULDBLOCK`/`EAGAIN`), and unlocking (`LOCK_UN`).
- Critical cleanup rule: Locks must automatically be released when the file descriptor is closed and when the process terminates.

### Permissions and Metadata (chmod, fchmod, chown, fchown) (Step 3 Focus)
- `chmod` and `chown` currently verify path existence via `access(path, 0)` but do not modify inode permissions.
- `fchmod` and `fchown` unconditionally return 0 without even inspecting the descriptor.
- Step 3 requires connecting `chmod`/`fchmod` to actual inode permissions on ext4 (and ramfs if supported), verifying observable changes via `stat()` / `fstat()`, and failing with `EBADF` for invalid descriptors.

### Offset-based Concurrent I/O (pread, pwrite) (Step 3 Focus)
- `pread` and `pwrite` currently do:
  ```c
  ssize_t pread(int fd, void* buf, size_t n, off_t off) {
      off_t cur = lseek(fd, 0, SEEK_CUR);
      if (cur < 0 || lseek(fd, off, SEEK_SET) < 0) return -1;
      ssize_t r = read(fd, buf, n);
      lseek(fd, cur, SEEK_SET);
      return r;
  }
  ```
- This design fails in concurrent multi-threaded applications (e.g. OpenJDK garbage collector or database readers) sharing the file descriptor: Thread A seeks to offset X; before Thread A reads, Thread B seeks to offset Y; Thread A reads from offset Y instead of X.
- Step 3 requires implementing true atomic offset operations in `kernel/app.c` / `sdk` where the internal descriptor cursor is untouched.

### Storage Flushing and Durability (sync, fsync, fdatasync)
- `fsync` and `fdatasync` have been successfully wired to `fd_sync` ABI version 22 (Task base-02).
- `sync()` remains an advisory flush; if global transaction syncing becomes available, it should invoke the underlying storage sync for all mounted writable filesystems.

---

## Verification and Test Harness

The inventory and status contract defined in this document are programmatically verified by
`tools/test_sdk_posix_matrix.py`. The test suite confirms:
1. Every documented fake success function exists in the SDK sources and has its status documented.
2. Every partial implementation function is tracked with its hazards and planned target.
3. Explicit `ENOSYS` and `ENOTSUP` functions fail-closed with standard error codes.
4. Future steps (flock implementation in Step 2, pread/pwrite/chmod in Step 3) can directly measure progress against this baseline.
