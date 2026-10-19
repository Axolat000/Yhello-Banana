#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# exFAT test: makes an empty exFAT image (mkfs.exfat), boots Banana OS in
# QEMU with it as a virtio disk (/mnt/virtio) and runs `fstest -k` there
# (shell/fstest.c: files, appends, a Unicode rename, 400 KiB that grows,
# 300 files in a folder, deletes, a remount, every byte read back). Then on
# Linux: fsck.exfat must find the volume clean, and an exFAT reader of its
# own (tools/exfat_check.py) checks what fstest left there.
#
# Inside Banana OS the same check runs on any drive: `fstest /mnt/usb`.
#
#   tools/test-exfat.sh [bios|uefi]          (needs exfatprogs)
#
# QEMU's window is shown (the commands go over the serial console; the
# screen is there to watch and debug); HEADLESS=1 runs it without one.
# KEEP=1 leaves it open after the commands - the checks on Linux run once
# its window is closed.

set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/test"
mkdir -p "$OUT" "$ROOT/build/logs"
IMG="$OUT/exfat.img"
CMDS="$OUT/exfat.cmds"

command -v mkfs.exfat >/dev/null && command -v fsck.exfat >/dev/null || { echo "needs exfatprogs (mkfs.exfat, fsck.exfat)"; exit 1; }

rm -f "$IMG"
truncate -s 64M "$IMG"
mkfs.exfat -L BANANA "$IMG" >/dev/null || exit 1

{
    echo "mount ## exFAT"
    echo "df ## exfat"
    echo "fstest -k /mnt/virtio ## 0 failed"
} > "$CMDS"

echo "== Booting with an exFAT disk"
"$ROOT/tools/dev.sh" build >/dev/null || { echo "build failed (tools/dev.sh build)"; exit 1; }
QEMU_ARGS=(-cdrom "$ROOT/Banana_OS.iso" -m "${MEM:-512M}" -smp "${SMP:-4}" -nic user,model=e1000
           -drive "file=$IMG,format=raw,if=virtio" -boot d)   # (the CD first: the image has no boot code)
[ "${1:-bios}" = uefi ] && QEMU_ARGS+=(-bios "${OVMF:-/usr/share/edk2/x64/OVMF.4m.fd}")
WINDOW=(--window)
[ "${HEADLESS:-0}" = 1 ] && WINDOW=()
[ "${KEEP:-0}" = 1 ] && WINDOW+=(--keep)
python3 "$ROOT/tools/serialtest.py" "${WINDOW[@]}" --log "$ROOT/build/logs/exfat.log" --cmds-file "$CMDS" -- "${QEMU_ARGS[@]}"
rc=$?

echo
echo "== On Linux: fsck.exfat"
fsck.exfat -n "$IMG" || rc=1
echo "== On Linux: what fstest left (-k)"
CHK=(python3 -I "$ROOT/tools/exfat_check.py" "$IMG")
"${CHK[@]}" list | grep -v '/many/f' || rc=1
n=$("${CHK[@]}" list | grep -c '/fstest/many/f')
echo "files in /fstest/many: $n (want 150)"; [ "$n" = 150 ] || rc=1
[ "$("${CHK[@]}" cat '/fstest/héllo wörld.txt')" = "hello world" ] && echo "héllo wörld.txt: hello world" || { echo "héllo wörld.txt: wrong"; rc=1; }
# fstest's 400 KiB: the same xorshift bytes, made here
python3 -I - "$OUT/expected.bin" <<'PY'
import sys
x = (7 * 2654435761 + 1) & 0xFFFFFFFF
out = bytearray()
for _ in range(400 * 1024):
    x ^= (x << 13) & 0xFFFFFFFF; x ^= x >> 17; x ^= (x << 5) & 0xFFFFFFFF
    out.append(x & 0xFF)
open(sys.argv[1], "wb").write(out)
PY
"${CHK[@]}" cmp /fstest/sub/big.bin "$OUT/expected.bin" || rc=1
[ $rc -eq 0 ] && echo "== exFAT: everything checks out" || echo "== exFAT: something failed (log: build/logs/exfat.log)"
exit $rc
