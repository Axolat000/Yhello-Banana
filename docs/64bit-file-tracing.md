# 64-Bit File Sizes and Offsets: Architectural Trace and Conversion Inventory

This document delivers Step 1 of task **base-19**:
> *"Trace file sizes from SDK down to ext4, catalog every 32-bit truncation / conversion bottleneck, and design the 64-bit ABI / VFS evolution required to support files larger than 4 GiB."*

---

## 1. Executive Summary

Banana OS currently enforces a maximum file size limit of `0x7FFFF000u` (~2 GiB, just under $2^{31}-1$) via `FS_MAX_FILE_SIZE` in [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L17). While the underlying ext4 implementation ([`kernel/ext4.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/ext4.c), [`kernel/ext4.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/ext4.h)) already uses 64-bit offsets and sizes (`uint64_t size`, `uint64_t off`, 48-bit extents, and 64-bit block numbers in `ext4_stat_t`), the intermediary VFS, mount dispatch table, kernel application boundary (`banana_api_t`), and POSIX SDK libraries truncate offsets and file lengths to 32 bits (`uint32_t` or signed 32-bit `long` on i686).

Allowing files larger than 4 GiB (and beyond 2 GiB) requires tracing and converting every layer of the call path, maintaining ABI compatibility for 32-bit applications, and keeping bounded streaming transfers so that memory consumption remains independent of file size.

---

## 2. End-to-End Tracing: Call Path from User Space to Disk

The data path flows through five primary tiers:
```
+-------------------------------------------------------------------+
| 1. SDK POSIX / C Standard Library                                  |
|    - libc / posix / stdio (sdk/lib/stdio.c, posix.c, unix.c)       |
|    - stat(), fstat(), lseek(), fseek(), ftell(), pread(), pwrite()|
+---------------------------------+---------------------------------+
                                  | (banana_api_t function pointers)
                                  v
+-------------------------------------------------------------------+
| 2. Banana Application ABI Boundary                                |
|    - sdk/include/banana_api.h (banana_api_t, banana_stat_t)       |
|    - kernel/app.c (syscall entry stubs, dispatch specs, afd_t)    |
|    - 32-bit (i686) vs 64-bit (x86_64) long representation         |
+---------------------------------+---------------------------------+
                                  | (Kernel VFS function calls)
                                  v
+-------------------------------------------------------------------+
| 3. Kernel VFS Layer                                               |
|    - kernel/fs.h (fs_file_t, fs_stat_t, FS_MAX_FILE_SIZE)         |
|    - kernel/fs.c (fs_read, fs_write_at, fs_truncate, fs_stat)      |
+---------------------------------+---------------------------------+
                                  | (fs_mount_ops_t function pointers)
                                  v
+-------------------------------------------------------------------+
| 4. Storage Backend Dispatch                                       |
|    - kernel/fsdisk.c (g_e4ops, op4_read_at, op4_write_at, etc.)   |
|    - Mounted external volumes (kernel/fat32.c, kernel/exfat.c)    |
+---------------------------------+---------------------------------+
                                  | (Direct driver calls)
                                  v
+-------------------------------------------------------------------+
| 5. ext4 Filesystem Driver                                         |
|    - kernel/ext4.c & kernel/ext4.h (ext4_read, ext4_write, etc.)  |
|    - Already 64-bit native (uint64_t size / off)                  |
+-------------------------------------------------------------------+
```

---

## 3. Comprehensive Inventory of 32-Bit Truncation Bottlenecks

### Layer 1: SDK POSIX and C Library Wrappers

| Location | Identifier / Symbol | Current Type / Definition | Truncation / Overflow Risk | Required Fix in Step 2 |
| :--- | :--- | :--- | :--- | :--- |
| [`sdk/include/sys/types.h`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/include/sys/types.h#L9) | `off_t` | `typedef long off_t;` | On 32-bit i686 architectures, `long` is 32 bits signed (max 2 GiB). | Update `off_t` to `int64_t` / `long long` on 32-bit, or provide `off64_t` LFS mapping. |
| [`sdk/include/sys/stat.h`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/include/sys/stat.h#L18) | `struct stat.st_size` | `off_t st_size;` | Truncated to 32 bits on i686 targets. | 64-bit integer type (`int64_t` / `off64_t`). |
| [`sdk/include/unistd.h`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/include/unistd.h#L8) | `lseek()` | `off_t lseek(int fd, off_t off, int whence);` | Restricts seek offset and return position to 32 bits on i686. | Accept 64-bit offset; forward to 64-bit ABI entry. |
| [`sdk/include/stdio.h`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/include/stdio.h) / [`sdk/lib/stdio.c`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/lib/stdio.c#L482) | `fseek()`, `ftell()` | `long off`, returns `long` | Standard C `fseek`/`ftell` cannot represent positions $\ge 2$ GiB on 32-bit. | Add `fseeko()`, `ftello()`, `fseeko64()`, `ftello64()` accepting 64-bit `off_t`. |
| [`sdk/lib/posix.c`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/lib/posix.c#L70) | `fstat()` | `long end = __banana->seek(fd, 0, SEEK_END);` | Calls `__banana->seek()` which truncates file size to signed `long`. | Query file info using 64-bit stat call instead of seek hack. |
| [`sdk/lib/posix.c`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/lib/posix.c#L94) | `stat()` | `st->st_size = b.size;` | `b.size` is `banana_stat_t.size` (`unsigned int`). Files > 4 GiB wrap modulo $2^{32}$. | Read 64-bit size from new stat ABI. |
| [`sdk/lib/unix.c`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/lib/unix.c#L233) | `ftruncate()` | `int ftruncate(int fd, off_t len)` | Uses `lseek` to seek to end, then loops writing zero bytes. | Connect to explicit truncate syscall / 64-bit truncate ABI. |

---

### Layer 2: Banana Application ABI Boundary (`banana_api.h` and `kernel/app.c`)

| Location | Identifier / Symbol | Current Definition | Truncation / Bottleneck Analysis | Required Fix in Step 2 |
| :--- | :--- | :--- | :--- | :--- |
| [`sdk/include/banana_api.h`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/include/banana_api.h#L151) | `banana_stat_t.size` | `unsigned int size;` | 32-bit unsigned int. Cannot represent files $\ge 4$ GiB. | Introduce `banana_stat64_t` or extend ABI with `stat64` entry; preserve legacy `stat` compatibility. |
| [`sdk/include/banana_api.h`](file:///var/lib/yhello/worktrees/base-19-s1/sdk/include/banana_api.h#L215) | `banana_api_t.seek` | `long (*seek)(int fd, long off, int whence);` | Uses `long`, which is 32 bits on i686. Negative offsets also conflated with error returns (-1). | Add `seek64` to `banana_api_t`: `long long (*seek64)(int fd, long long off, int whence)`. |
| [`kernel/app.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/app.c#L62) | `afd_t.pos` | `uint32_t pos;` | Per-process file descriptor position is truncated to 32 bits. | Widen `afd_t.pos` to `uint64_t`. |
| [`kernel/app.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/app.c#L626) | `a_read()` | `fs_read(f->fidx, f->pos, buf, len)` | Passes 32-bit `f->pos`; updates `f->pos += (uint32_t)n;`. | Passes 64-bit offset to 64-bit VFS read; updates `uint64_t f->pos`. |
| [`kernel/app.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/app.c#L640) | `file_write()` | `if (len > 0xFFFFFFFFul - f->pos) return -1;` | Enforces explicit 32-bit overflow check; casts offset to `(uint32_t)len`. | Expand offset checks to 64-bit (`uint64_t`). |
| [`kernel/app.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/app.c#L649) | `a_seek()` | `long base = ... np = base + off; f->pos = (uint32_t)np;` | Truncates target position to `uint32_t`. | Handle 64-bit calculations with proper range checking. |
| [`kernel/app.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/app.c#L841) | `a_stat()` | `st->size = fs_file_info(f)->size;` | Assigns to 32-bit `st->size`. Caps at 4 GiB max (or truncates). | Add `a_stat64` handler writing 64-bit file size. |
| [`kernel/app.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/app.c#L1810) | `g_spec` syscall table | `SPEC(seek, "iii", 0);` | Syscall signature `iii` treats offset as 32-bit integer register. | Register 64-bit variant with argument specification supporting 64-bit parameters (`L` or struct). |

---

### Layer 3: Kernel Virtual File System (VFS: `fs.h` and `fs.c`)

| Location | Identifier / Symbol | Current Definition | Truncation / Bottleneck Analysis | Required Fix in Step 2 |
| :--- | :--- | :--- | :--- | :--- |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L17) | `FS_MAX_FILE_SIZE` | `#define FS_MAX_FILE_SIZE 0x7FFFF000u` | Hard limit of 2,147,479,552 bytes (~2 GiB). Rejects all creations and writes past 2 GiB. | Increase or split `FS_MAX_FILE_SIZE` to allow 64-bit disk-backed files (up to volume capacity), keeping heap-buffering limit distinct from streaming disk files. |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L32) | `fs_file_t.size` | `uint32_t size;` | Truncates stored file length to 32 bits ($< 4$ GiB). | Change `fs_file_t.size` to `uint64_t`. |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L33) | `fs_file_t.cap` | `uint32_t cap;` | Heap capacity buffer for files in RAM. (Files $> 4$ GiB must NOT be loaded whole into RAM heap!). | Keep `cap` for in-memory heap buffering, but streaming disk files must keep `content == NULL` and stream directly. |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L248) | `fs_mount_ops_t.read_at` | `uint32_t off` | Mount operation takes 32-bit offset. | Change `off` parameter to `uint64_t off`. |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L249) | `fs_mount_ops_t.write_at` | `uint32_t off` | Mount operation takes 32-bit offset. | Change `off` parameter to `uint64_t off`. |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L250) | `fs_mount_ops_t.truncate` | `uint32_t size` | Mount operation takes 32-bit target size. | Change `size` parameter to `uint64_t size`. |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L261) | `fs_write_at()` | `int fs_write_at(int idx, uint32_t off, const void* data, uint32_t len)` | Takes 32-bit offset. | Update prototype to `uint64_t off`. |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L263) | `fs_truncate()` | `int fs_truncate(int idx, uint32_t size)` | Takes 32-bit size. | Update prototype to `uint64_t size`. |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L196) | `fs_read()` | `int fs_read(int idx, uint32_t off, void* buf, uint32_t len)` | Takes 32-bit offset. | Update prototype to `uint64_t off`. |
| [`kernel/fs.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.h#L285) | `fs_stat_t.size` | `uint32_t size;` | Truncates stat size. | Update `fs_stat_t.size` to `uint64_t`. |
| [`kernel/fs.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fs.c#L461) | `reserve()` | `krealloc(f->content, cap)` | Attempts to allocate file size + 1 in contiguous kernel heap memory. | Enforce that files exceeding RAM/streaming threshold (`STREAM_MIN`) bypass RAM buffering. |

---

### Layer 4: Storage Backend Dispatch (`kernel/fsdisk.c`)

| Location | Identifier / Symbol | Current Definition | Truncation / Bottleneck Analysis | Required Fix in Step 2 |
| :--- | :--- | :--- | :--- | :--- |
| [`kernel/fsdisk.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fsdisk.c#L1528) | `op4_read_at()` | `int op4_read_at(void* c, uint32_t node, uint32_t off, void* buf, uint32_t len)` | Truncates offset to 32 bits before calling `ext4_read`. | Change signature to `uint64_t off`. |
| [`kernel/fsdisk.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fsdisk.c#L1532) | `op4_write_at()` | `int op4_write_at(void* c, uint32_t node, uint32_t off, const void* data, uint32_t len)` | Truncates offset to 32 bits before calling `ext4_write`. | Change signature to `uint64_t off`. |
| [`kernel/fsdisk.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fsdisk.c#L1536) | `op4_truncate()` | `int op4_truncate(void* c, uint32_t node, uint32_t size)` | Truncates size to 32 bits before calling `ext4_truncate`. | Change signature to `uint64_t size`. |
| [`kernel/fsdisk.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/fsdisk.c#L1600) | `e4_load_dir()` | `if ((t == EXT4_S_IFREG \|\| t == EXT4_S_IFLNK) && st.size <= FS_MAX_FILE_SIZE)` | Directory scanner discards files larger than `FS_MAX_FILE_SIZE`! | Remove or adjust `FS_MAX_FILE_SIZE` guard so large files on disk are indexed. |

---

### Layer 5: ext4 Filesystem Driver (`kernel/ext4.c` and `ext4.h`)

| Location | Identifier / Symbol | Current Definition | Status & Assessment |
| :--- | :--- | :--- | :--- |
| [`kernel/ext4.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/ext4.h#L49) | `ext4_stat_t.size` | `uint64_t size;` | **Already 64-bit compliant.** |
| [`kernel/ext4.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/ext4.h#L52) | `ext4_stat_t.blocks` | `uint64_t blocks;` | **Already 64-bit compliant.** |
| [`kernel/ext4.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/ext4.h#L97) | `ext4_read()` | `long ext4_read(uint32_t ino, uint64_t off, void* buf, uint32_t len)` | **Already 64-bit compliant.** Supports reading at 64-bit byte offsets. |
| [`kernel/ext4.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/ext4.h#L99) | `ext4_write()` | `int ext4_write(uint32_t ino, uint64_t off, const void* buf, uint32_t len)` | **Already 64-bit compliant.** Supports writing at 64-bit offsets and growing files. |
| [`kernel/ext4.h`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/ext4.h#L100) | `ext4_truncate()` | `int ext4_truncate(uint32_t ino, uint64_t size)` | **Already 64-bit compliant.** Truncates or extends extent tree with 64-bit sizes. |
| [`kernel/ext4.c`](file:///var/lib/yhello/worktrees/base-19-s1/kernel/ext4.c#L1100) | Inode read / write | `i_size_lo` and `i_size_high` | Uses standard ext4 dual 32-bit fields (`(uint64_t)raw->i_size_high << 32 \| raw->i_size_lo`) up to 16 TiB. |

---

## 4. ABI Evolution Strategy for 64-Bit File Support

To ensure backwards compatibility for existing binary applications running on Banana OS:

1. **ABI Version Bump & Table Extension**:
   - Increment `BANANA_API_VERSION` from `22` to `23`.
   - Add new 64-bit aware entries at the end of `banana_api_t`:
     ```c
     /* v23: 64-bit file operations */
     long long (*seek64)(int fd, long long off, int whence);
     int       (*stat64)(const char* path, banana_stat64_t* st);
     int       (*truncate64)(int fd, unsigned long long size);
     ```
   - Define `banana_stat64_t`:
     ```c
     typedef struct {
         unsigned long long size;     /* 64-bit bytes */
         int                is_dir;
         unsigned int       mode;
         unsigned int       mtime;
     } banana_stat64_t;
     ```

2. **Syscall Argument Dispatch in `kernel/app.c`**:
   - For `seek64` on 32-bit (i686) ring 3 stubs, pass `off` as low/high 32-bit halves or via register pairs, or dispatch cleanly via struct pointer.
   - For legacy `stat` callers: cap file sizes $\ge 4$ GiB at `0xFFFFFFFFu` or return `EOVERFLOW`, avoiding silent modulo truncation.

3. **POSIX Layer Integration**:
   - In `sdk/include/sys/types.h`, alias `off64_t` to `long long`.
   - Provide `lseek64`, `fseeko64`, `ftello64`, `stat64`, `fstat64`.
   - Update `fstat` in `sdk/lib/posix.c` to query the descriptor's true 64-bit size directly from `a_stat64` rather than seeking.

---

## 5. Verification and Acceptance Criteria for Step 2 and Step 3

1. **Step 2 (VFS & Backend Conversion)**:
   - Convert `fs_file_t.size`, `fs_stat_t.size`, `fs_read`, `fs_write_at`, `fs_truncate` to 64-bit.
   - Update `fs_mount_ops_t` and `g_e4ops`.
   - Remove the `FS_MAX_FILE_SIZE` 2 GiB ceiling on disk-backed mounts, replacing it with volume capacity checks.
   - Ensure bounded block streaming: RAM usage must stay constant regardless of file size.

2. **Step 3 (Streaming & Format Boundaries)**:
   - Streaming copy (`fs_copy` / `cp`), downloads, and verification.
   - Distinct handling of filesystem format limits: ext4 supports $>4$ GiB; FAT32 cleanly rejects files $\ge 4$ GiB with explicit `EFBIG` / `ENOSPC`.
   - Verification test creating, writing patterns before/after 2 GiB and 4 GiB boundaries (e.g. 5 GiB file), seeking, truncating, and checking SHA-256 / block hashes.
