# Bounded virtio-scsi adaptation

Source: upstream commit `aa1a60e07980cfca808fec71b261bfd645de6e8e`;
base fork: `fe73f98e2bf9aef877b4fdea5f8f73a724d6fd7c`. Its parent
`6d8dc7f` is already an ancestor of the fork; the intervening diff consists of
workflow and planning documents, so no upstream driver prerequisite is missing.
Only the SCSI driver and necessary
boot, disk and presentation interfaces are adapted. SDHCI, MMC, ADMA2 and their
installation changes are excluded. Repository BSD-2-Clause licensing is retained.
No dependency, build configuration or workflow change is required.

## Contract and limits

The existing virtio PCI transport supports legacy and modern devices. The driver
uses request queue 2, one outstanding command per controller, a kernel bounce
buffer and controller serialization. It discovers disks only at boot, without
hotplug, event buffers, multiqueue, bidirectional I/O or protection information.
The limits are four controller slots (including unsuccessful startup), eight
registered disks, targets 0–63 and LUNs 0–15 on channel zero. Larger discovery
hints produce a serial notice. Every in-range LUN is probed independently, including
LUN 1 without a usable LUN 0. Other channels are rejected. Unsupported disks are
not installation targets.

Only 512-byte logical sectors and capacities of 1 through `UINT32_MAX` sectors
are accepted. Larger disks are rejected rather than silently truncated. Capacity
is read with READ CAPACITY(10), falling back to READ CAPACITY(16). Data uses
READ/WRITE(10), at most 256 sectors (128 KiB) per command, further reduced by
`max_sectors`. The driver requires at least one request queue, data segment and
command per LUN, nonzero `max_sectors`, and the reset-default CDB/sense sizes of
32/96. It does not alter device configuration.

Completion requires one used entry for descriptor head zero. Modern used lengths
must cover response fields, declared sense and transferred input bytes, without
exceeding writable capacity. One exact compatibility exception is accepted:
QEMU includes the data-out size in its used length even for modern devices.
The alternate total must equal the full response allocation plus the submitted
output bytes; it is normalized to the response size before validation. See
[QEMU virtio-scsi completion](https://github.com/qemu/qemu/blob/v10.0.0/hw/scsi/virtio-scsi.c#L110).
Residual and SCSI status checks remain mandatory; arbitrary oversized lengths
still fail closed. Legacy used lengths are ignored as recommended by
[Virtio 1.3 §5.6.6.0.1 and §5.6.6.1](https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html).
Both interfaces validate sense bounds and residuals; GOOD with a nonzero residual
fails without copying any read data to the caller. Fixed/descriptor sense is
parsed only after length validation; truncated or unknown sense cannot trigger a
retry. No response tag is echoed by this protocol: association is established by
the used descriptor identity and the single outstanding command.

Timeout, unexpected used indices, wrong head or malformed response framing
permanently fail the controller closed. Its slot, ring, request, response and
bounce allocations are retained until reboot. FAILED status is **not** treated as
confirmed DMA quiescence. Subsequent reads, writes and flushes fail before touching
that storage, even if a late completion arrives. There is no reset/recovery path.
Partial startup allocations are also retained; this is bounded by controller slots.

SYNCHRONIZE CACHE errors, including ILLEGAL REQUEST, propagate as failure. No
cache-disabled assumption converts rejection into success. The block-device write
callback flushes after a successful write; the installed filesystem uses the
existing `disk_flush` route. This does not repair the separate application
`fsync`/`fdatasync` obligation tracked by `base-02`.

`ata_disk_t.scsi` is an internal kernel discriminator: zero for other devices,
SCSI index plus one otherwise. ATA explicitly initializes it; AHCI, NVMe and
virtio-blk zero the entire structure. All consumers must be rebuilt. There is no
user ABI, on-disk format, ext4/JBD2 layout or migration change. `disk_same` and
`disk_blockdev` preserve system-volume automount exclusion for SCSI disks.

## Verification

`python3 tools/test_virtio_scsi.py --sanitize` compiles the production driver into
a host harness with mock PCI, queue and device responses. It tests short/residual
transfers without caller-buffer modification, malformed completion identity,
length, index delta and sense, flush rejection, timeout followed by late DMA,
no post-failure descriptor/buffer reuse, retention of failed controller slots,
configuration/transfer limits, allocation/registration failure, capacity bounds,
non-512-byte rejection, LUN 1 discovery, chunked byte comparison, 16-bit queue
index wrap and four concurrent host callers. These are host tests, not evidence
of device execution in QEMU or on hardware. `--32` runs the same harness with
`-m32`; an execution restriction must be reported separately from compilation.

The proposed checklist follow-up `virtio-scsi-install` retains the full VM and
power-cut qualification requirements and depends on `virtio` and `base-02`.
Its scheduling requires independent review and controller validation. Existing
checklist history and started tasks are unchanged. Virtio-blk remains the
reference VM controller. No performance or physical-hardware claim is made.

Reproduce the QEMU smoke test after `make -j4`:

```sh
python3 tools/test_virtio_scsi_qemu.py --transport legacy --firmware bios
python3 tools/test_virtio_scsi_qemu.py --transport modern --firmware bios
python3 tools/test_virtio_scsi_qemu.py --transport legacy --firmware uefi
python3 tools/test_virtio_scsi_qemu.py --transport modern --firmware uefi
```

The script uses TCG, four CPUs, 1 GiB RAM, explicit writeback caching and two
SCSI controllers carrying three FAT disks at target/LUN 0/1, 1/0 and 2/2.
It checks the copied file with SHA-256 on the host and hashes both non-target
disks before and after. `run.json` records the complete command and QEMU version;
`serial.log` records discovery, copy and selftest/POSIX results. This is a live-CD
smoke test, not installed-system qualification or a physical-device test.

Installation and synchronized-file persistence can be reproduced separately:

```sh
python3 tools/test_virtio_scsi_install.py --firmware bios
python3 tools/test_virtio_scsi_install.py --firmware uefi
```

These use a disposable 1 GiB install target at LUN 1, two hashed FAT data disks,
modern SCSI and explicit writeback caching. The install and file-copy phases end
with a successful `sync`, followed by the runner killing QEMU. Subsequent phases
boot without a CD, check the saved marker, require clean guest poweroff and boot
again. A same-ISO update comparison is exercised; it does not write an update.
Linux `e2fsck -fn` checks the extracted ext4 region, and `debugfs` extracts the
copied file for comparison with its source SHA-256. Both non-target whole-image
hashes must remain unchanged. Every phase command is retained in `run.json`.

UEFI tests use read-only `OVMF_CODE_4M.fd` and a private copy of
`OVMF_VARS_4M.fd` as pflash drives. The initial combined-ROM test failed its
non-target hash check because OVMF wrote `NvVars` to the first data FAT volume.
This firmware fallback is avoided with a private variable store; it must not be
misreported as a passing non-target check.

The adjacent `tools/serialtest.py` changes are necessary for these tests: accept
an initial prompt followed by asynchronous automount output, exclude only the
exact successful POSIX stack-overflow recovery line from crash matching, and
optionally require actual QEMU exit zero after guest `shutdown now`.

## Qualification still required

The assigned upstream substep remains incomplete until the remaining checks are
executed. Host mock responses establish timeout/late-completion safety and
malformed-response handling; QEMU fault injection remains to be done. Concurrent
SCSI callers and index wrap have host coverage only. Actual update writes,
journal-boundary interruption with writeback caching, filesystem/application
flush-error propagation (application coverage depends on `base-02`), and the full
IDE/SATA/NVMe/virtio-blk/removable PIO SD regression matrix remain outstanding.
Physical hardware and performance have not been qualified. The checklist proposal
does not remove these obligations or mark the upstream substep complete.

## Execution record (2026-10-19)

- `make -j4 kernel.bin kernel64.bin`: both complete kernels linked; log
  `build/logs/virtio-scsi-build.log`. The existing embedded applications and
  FFmpeg were rebuilt. `make -j4 SB_KEY=/nonexistent` then rebuilt the final
  kernels and unsigned test ISO: `build/logs/virtio-scsi-iso.log`.
  Artifact hashes are in `build/logs/virtio-scsi-built-sha256.txt`; generated
  tracked binaries are restored before committing source.
- `python3 tools/test_virtio_scsi.py --sanitize`: passed with ASan/UBSan;
  `build/logs/virtio-scsi-host64.log`. `--32` compiled but execution terminated
  with SIGSYS; `build/logs/virtio-scsi-host32.log`. No 32-bit host pass is claimed.
- `sh tools/ext4test/run.sh`: all passed, including its journal replay and
  Linux e2fsck checks; `build/logs/virtio-scsi-ext4.log`. This is an ext4 host
  regression, not a SCSI VM fault-injection test.
- All four QEMU smoke combinations above passed on QEMU 10.0.13 (Debian
  `1:10.0.13+ds-0+deb13u1`). Each reported selftest 176 passed / 0 failed and
  POSIX 82 passed / 0 failed, matched the copied file hash, and preserved both
  non-target image hashes. Console logs are
  `build/logs/virtio-scsi-qemu-{legacy,modern}-{bios,uefi}.log`; complete command
  records and serial logs are under
  `build/test/virtio-scsi/qemu-{legacy,modern}-{bios,uefi}/`.
- Both modern installation combinations passed, including guest poweroff and
  restart, sync followed by VM interruption, exclusion/data mounting, two
  non-target image hashes, same-ISO update comparison and Linux e2fsck.
  Logs: `build/logs/virtio-scsi-install-modern-{bios,uefi}.log`; phase commands,
  serial logs and hashes: `build/test/virtio-scsi/install-modern-{bios,uefi}/`.
  The copied 265801-byte file SHA-256 in all six runs was
  `64b993e40299eab00bbf5397bcb70ea9aace6ff6177af9d7dab748c582177424`.

Initial runs exposed the runner issues and QEMU used-length compatibility
behavior described above. Corrected runs replaced them as passing evidence;
selected initial logs remain in `build/logs/virtio-scsi-initial/`.
Antigravity delegation was attempted once, but its helper rejected the local
configuration ownership/permissions check. No proposal was returned or applied,
and the helper configuration was not changed. Implementation and verification
were performed locally.

Commit creation was prevented by the execution environment: Git could not create
its worktree `index.lock` because that metadata directory was read-only. Source
changes remain in the worktree. A complete source patch is retained in
`build/logs/virtio-scsi-progress.patch`; no commit or push is claimed. The tested
ISO is retained separately at `build/test/virtio-scsi/tested.iso`.
