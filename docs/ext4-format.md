# Banana OS ext4 and JBD2 implementation

This describes the subset in `kernel/ext4.c`, rather than all features Linux
can enable in ext4. The installed-system adapter is `kernel/fsdisk.c`.

## Geometry and objects

The driver uses 4096-byte blocks, 32768 blocks per group, 256-byte inodes and
32-byte group descriptors. Its device callbacks address 512-byte sectors
relative to the filesystem, using a 32-bit sector count. Extent trees map
regular files and directories; directories use linear entries with file types.
Root is inode 2. Short symbolic links fit in the inode; longer ones use blocks.
Hard links to non-directory objects are supported (`ext4_link`).

The installed disk's filesystem start is not universally 128 MiB. In
`fsdisk.c`, legacy layouts retain that boundary; new installations describe
the system partition in MBR entry 2 and reserve a power-of-two boot area
between 256 MiB and 1 GiB, sized for the image. Device callbacks receive
sectors relative to the selected filesystem start. Do not mount every current
installation with a hard-coded offset copied from older documentation.

## Feature acceptance

`mount_sb` accepts only the incompatible flags `FILETYPE` (0x0002), `EXTENTS`
(0x0040), `FLEX_BG` (0x0200), and transient `RECOVER` (0x0004). `EXTENTS` is
required; any incompatible bit outside this mask rejects the volume.

The accepted read-only-compatible mask is `SPARSE_SUPER` (0x0001),
`LARGE_FILE` (0x0002), `GDT_CSUM` (0x0010), `DIR_NLINK` (0x0020) and
`EXTRA_ISIZE` (0x0040). Other read-only-compatible bits reject the mount;
the driver does not silently fall back to a read-only mount.

The formatter sets the native incompatible mask, the above read-only-compatible
mask, `SPARSE_SUPER2`, and `HAS_JOURNAL` once the journal is created. Its
`SPARSE_SUPER2` layout does not provide backup superblocks. Block/inode bitmaps,
inode tables and extent metadata use the implementation's native format and
checksum policy; modern ext4 defaults such as metadata checksums and 64-bit
group descriptors must not be assumed compatible.

The `LARGE_FILE` on-disk flag is not a promise that application I/O handles
files of arbitrary size. `kernel/fs.h` still limits individual VFS files to
`0x7FFFF000` bytes and uses 32-bit size/offset fields in several interfaces.

## Journal ordering and replay

The internal journal is inode 8, with a JBD2 v2 superblock. `jload` also
recognizes the older journal superblock type, while rejecting journal feature
flags it cannot implement. Journal structures use big-endian fields, unlike
the little-endian filesystem structures. Descriptor tags identify the target
metadata blocks; escape handling prevents payload magic from being confused
with a journal header. Recovery also processes revoke records.

`jcommit` writes file data before committing the corresponding metadata:
metadata copies and descriptors go to the journal, a device flush precedes
the commit record, and another flush precedes checkpointing metadata in place.
The journal is then marked empty and recovery state cleared. This is ordered
journaling, not data journaling of every file byte. An incomplete transaction
must not be treated as committed; a complete transaction can be replayed on
mount. The guarantee depends on the underlying device honoring its flush.

## Checking compatibility

`sh tools/ext4test/run.sh` compiles the real driver with host device shims,
creates and exercises disposable images, reopens them, and checks them with
Linux `e2fsck`. It also exercises journal recovery on interrupted writes.
Run the harness for the exact revision under review; a format description or
an accepted feature bit is not evidence of passing crash or hardware tests.

Host images begin at their filesystem's sector zero. Installed disks include
the boot area described above, so use their partition table when inspecting
them externally. Work on a copy before using a repairing filesystem checker.
