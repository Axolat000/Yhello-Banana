# Installer boot area

Build 249's published ISO is 164,177,920 bytes. The previous installer
allowed only 134,184,960 bytes (128 MiB minus its checksum reservation).

New installs reserve a power-of-two boot area, starting at 256 MiB and
growing to 512 MiB or 1 GiB when needed. The image must end before the
128 KiB checksum reservation. The filesystem starts after this boot area;
MBR entry 2 records its start and size as a Linux (0x83) partition.
Entry 1 remains the EFI system partition, so BIOS and UEFI boot are kept.

The image stream includes the target's filesystem partition entry before
hashing, comparing or writing. Updates therefore preserve the partition and
the checksums cover the bytes actually written, including sector zero.

Legacy layouts without this partition retain their 128 MiB boundary and
their original checksum-table format. Updates check the target boundary
before writing. An image too large for an existing installation is still
rejected: this change does not relocate existing filesystems. Back up files
and reinstall to obtain the larger boot area.

Run the host regression tests with: sh tools/tests/fsdisk_boot.sh

The tests exercise the real installer geometry and checksum code, including
the 156.6 MiB image, exact size boundaries, invalid partition entries, and
new and legacy checksum-table round trips and corruption detection.

Validation on the build machine (2026-10-23):
- Both i386 and x86_64 kernels compile; full hybrid ISO built.
- Host regression tests pass under AddressSanitizer and UBSan.
- Full installation onto a new 3 GiB QEMU disk succeeds.
- Ext4 starts at 256 MiB; BIOS boot without the CD reads a persisted file.
- Updating a deliberately changed boot image preserves the MBR filesystem
  partition; the persisted file remains after reboot.
- UEFI boot from the installed disk succeeds.
- Banana OS fsck and Linux e2fsck -fn report no errors.

The GNU/editor packages were reused from the official build-215 ISO for
the same source commit (c72e216); both kernels and the loaders were rebuilt.
