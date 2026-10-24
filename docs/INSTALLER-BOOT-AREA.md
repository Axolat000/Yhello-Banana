# Installer boot area

Build 249's published ISO is 164,177,920 bytes. The previous installer
allowed only 134,184,960 bytes (128 MiB minus its checksum reservation).

New installs reserve at least **1 GiB** for the boot image. The installer
requires spare space equal to **50% of the image size or 256 MiB**, whichever
is larger, in addition to the 128 KiB checksum reservation. It rounds the
result up to a power of two (1 GiB or 2 GiB). The current roughly 157 MiB
ISO therefore has about 867 MiB of growth room.

The checksum format supports images up to 1 GiB; larger images are rejected
before any disk write, even when the reserved area is larger. The filesystem
starts after the boot area; MBR entry 2 records its start and size as a Linux
(0x83) partition. Entry 1 remains the EFI system partition, so BIOS and UEFI
boot are kept. The destination must also have room for the filesystem and
the selected files; installation does not reduce the growth margin to fit
a smaller disk.

The image stream includes the target's filesystem partition entry before
hashing, comparing or writing. Updates therefore preserve the partition and
the checksums cover the bytes actually written, including sector zero.

Existing 256 MiB, 512 MiB and 1 GiB partitions remain readable and are
not resized during updates. Legacy layouts without this partition retain their 128 MiB boundary and
their original checksum-table format. Updates check the target boundary
before writing. The hybrid image now shares its kernel data between the ISO9660 tree and
the FAT EFI partition. Previously both kernels were stored twice. The new
image is about 80 MiB instead of 157 MiB, leaving about 48 MiB free in a
legacy 128 MiB boot area. Thus older installations can update without moving
their filesystem or reinstalling. Both the Rock Ridge and Joliet trees point
to the same aligned, contiguous kernel bytes used by UEFI.

The image builder verifies alignment, contiguity and identical contents
before removing duplicate extents, updates both volume sizes and preserves
other files. Unsupported layouts fail the build. The checksum image limit
also applies during updates. Images genuinely larger than an existing boot
area are still rejected before writing; no filesystem relocation is attempted.

Run the host regression tests with: sh tools/tests/fsdisk_boot.sh

The tests exercise the real installer geometry and checksum code, including
the 156.6 MiB image, growth margins across all supported image sizes, threshold boundaries,
all old and new partition sizes, invalid partition entries, and
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

Validation of shared kernel data:
- Run python3 tools/test_mkimage.py (mtools and xorriso required).
- Run BASE_ISO=/path/to/old-128MiB-installable.iso bash tools/ci-install-test.sh bios
  and repeat with uefi to test the legacy upgrade path and persisted files.
