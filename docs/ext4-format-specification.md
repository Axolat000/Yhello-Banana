# ext4 on-disk format and feature compatibility specification

This document details the ext4 filesystem and JBD2 journaling format produced by Banana OS (`kernel/ext4.c`), as well as feature compatibility policies (accepted vs. rejected feature sets) when mounting internal or external volumes.

## 1. Filesystem layout produced by `ext4_mkfs`

`ext4_mkfs(const ext4_dev_t* dev, const char* label, const uint32_t (*keep)[2], int nkeep)` formats a block device using ext4 with JBD2 journaling enabled:

- **Block size**: 4096 bytes (`BS = 4096`, 8 sectors per block).
- **Block group size**: 32768 blocks per group (`BPG = 32768`, 128 MiB per group, matching one 4 KiB allocation bitmap).
- **Inode size**: 256 bytes (`ISZ = 256`). Group 0 allocates `ipg` inodes (default 2048, scaled to 1/16th of blocks per group, or at least 1024).
- **Group descriptor size**: 32 bytes (`DESC = 32`, 32-bit descriptor table layout; 64-bit descriptors are not used).
- **Extent tree**: Extents (`INCOMPAT_EXTENTS`) are used for mapping all file and directory blocks.
- **Flex block groups**: `INCOMPAT_FLEX_BG` with flex group size 16 (`s_log_groups_per_flex = 4`), placing block bitmaps, inode bitmaps, and inode tables together at the start of each flex group.
- **Superblock layout**:
  - `s_magic`: `0xEF53`.
  - `s_rev_level`: `1` (dynamic revision).
  - `s_state`: `1` (`EXT4_VALID_FS`).
  - `s_errors`: `1` (`EXT4_ERRORS_CONTINUE`).
  - `s_first_ino`: `11` (`EXT4_LPF_INO`, `lost+found`). Inodes 1–10 are reserved. Root inode is 2 (`EXT4_ROOT_INO`).
  - Inode 8 is dedicated to the JBD2 journal (`JOURNAL_INO`).
  - UUID: Random version-4 UUID.
  - Directory hashing: Half-MD4 hash algorithm (`s_def_hash_version = 1`), signed directory hash (`s_flags |= 1`).

## 2. JBD2 Journal format produced and supported

Banana OS creates and supports a standard JBD2 journal on inode 8:
- **Journal version**: JBD2 superblock version 2 (`JBD2_SB_V2 = 4`, with compatibility check accepting type 3 as well).
- **Journal sizing**: 1024 blocks (4 MiB) for filesystems under 50 MiB; otherwise 8192 blocks (32 MiB).
- **Block tags & records**:
  - `JBD2_DESCRIPTOR` (type 1): block tags carrying target block numbers, with escape handling (`JF_ESCAPE`) when user data begins with `JBD2_MAGIC` (`0xC03B3998`).
  - `JBD2_COMMIT` (type 2): transaction completion marker with sequence counter and timestamp.
  - `JBD2_REVOKE` (type 5): revoke block list for blocks freed and repurposed within active transactions.
- **Recovery and replay**: Mount replays committed transactions found in the journal if not cleanly closed, and clears `INCOMPAT_RECOVER` upon successful recovery.

## 3. Superblock feature flags

### A. Features Produced by `ext4_mkfs`
- **Incompatible (`s_feature_incompat`)**:
  - `INCOMPAT_FILETYPE` (`0x0002`): Directory entries record inode file types.
  - `INCOMPAT_EXTENTS` (`0x0040`): File allocation uses extent trees rather than indirect block maps.
  - `INCOMPAT_FLEX_BG` (`0x0200`): Flex block groups.
- **Read-only compatible (`s_feature_ro_compat`)**:
  - `RO_SPARSE_SUPER` (`0x0001`): Superblock backups only in specific groups.
  - `RO_LARGE_FILE` (`0x0002`): Files larger than 2 GiB supported.
  - `RO_GDT_CSUM` (`0x0010`): CRC16 checksums on group descriptors (`uninit_bg`).
  - `RO_DIR_NLINK` (`0x0020`): Directory reference counts exceeding 32000.
  - `RO_EXTRA_ISIZE` (`0x0040`): Inode structures have extra field space beyond 128 bytes.
- **Compatible (`s_feature_compat`)**:
  - `COMPAT_SPARSE_SUPER2` (`0x0200`): Superblock backup block numbers recorded directly in superblock fields (primary superblock at block 0/group 0; backup count set to 0).
  - `COMPAT_HAS_JOURNAL` (`0x0004`): JBD2 journaling active on inode 8.

### B. Compatibility Matrix for External Volumes

When mounting an existing filesystem image or external disk partition via `ext4_mount()`:

| Feature Category | Flag Name | Value | Driver Policy | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Incompat** | `INCOMPAT_FILETYPE` | `0x0002` | **Accepted** | Required directory entry format. |
| **Incompat** | `INCOMPAT_EXTENTS` | `0x0040` | **Required** | The driver mounts only extent-mapped files; legacy indirect blocks are rejected. |
| **Incompat** | `INCOMPAT_FLEX_BG` | `0x0200` | **Accepted** | Standard block group allocation grouping. |
| **Incompat** | `INCOMPAT_RECOVER` | `0x0004` | **Accepted (Transient)** | Journal recovery is performed during mount, after which this flag is cleared. |
| **Incompat** | *Any other incompat flag* | | **REJECTED** | E.g., `INCOMPAT_64BIT` (`0x0080`), `INCOMPAT_MMP` (`0x0100`), `INCOMPAT_INLINE_DATA` (`0x0200`), `INCOMPAT_ENCRYPT` (`0x0800`), `INCOMPAT_CSUM_SEED` (`0x1000`). Mount fails with `-1`. |
| **RO Compat** | `RO_SPARSE_SUPER` | `0x0001` | **Accepted** | Sparse superblock distribution. |
| **RO Compat** | `RO_LARGE_FILE` | `0x0002` | **Accepted** | Large file sizing fields. |
| **RO Compat** | `RO_GDT_CSUM` | `0x0010` | **Accepted** | Uninitialized block groups and descriptor CRC16 verified. |
| **RO Compat** | `RO_DIR_NLINK` | `0x0020` | **Accepted** | Directories with > 32000 links. |
| **RO Compat** | `RO_EXTRA_ISIZE` | `0x0040` | **Accepted** | Extra inode fields (nanosecond timestamps, create time). |
| **RO Compat** | *Any other ro_compat flag* | | **REJECTED** | Because the driver mounts read-write, any unhandled ro_compat flag (e.g., `RO_METADATA_CSUM` `0x0400`, `RO_READONLY` `0x1000`, `RO_PROJECT` `0x2000`) causes mount rejection. |
| **Compat** | `COMPAT_SPARSE_SUPER2`| `0x0200` | **Accepted** | Superblock backup positions. |
| **Compat** | `COMPAT_HAS_JOURNAL` | `0x0004` | **Accepted** | Supported when journal is on internal inode 8 (`s_journal_inum == 8`). External journal UUID devices are logged and not attached. |
| **Compat** | *Other compat flags* | | **Tolerated / Ignored** | Standard ext4 compat flags that do not restrict read-write operations (such as `COMPAT_DIR_INDEX` / htree directory indexing) are safely tolerated for linear lookups or directory traversal. |
| **JBD2 Incompat** | Incompat flags in journal superblock | | **REJECTED if replay needed** | If journal superblock contains unsupported feature flags (e.g., checksum v2/v3, 64-bit tags) and has uncommitted/pending records, replay fails and mount is aborted. |

## 4. Verification and Error Reporting

- Superblock sanity checks verify magic `0xEF53`, block size 4096, 256-byte inodes, and group descriptor size 32.
- Violations or unsupported incompatible features emit descriptive diagnostics via `klog()` before returning `-1`:
  - `ext4: features this driver does not have (incompat %x, ro_compat %x)`
  - `ext4: an external journal - not used`
  - `ext4: the journal needs a replay this driver cannot do`
  - `ext4: the journal has features this driver does not write - not journaling`
