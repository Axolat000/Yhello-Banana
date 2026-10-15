#!/usr/bin/env bash
# exFAT test: makes an empty exFAT image (mkfs.exfat), boots Banana OS in
# QEMU with it as a virtio disk (/mnt/virtio), and has Banana OS write to it
# over the serial console: files, a folder, appends, a rename to a Unicode
# name, a copy of a binary file, 60 files (so a folder outgrows its first
# cluster), a delete; then it unmounts and mounts the volume again and reads
# it all back. Afterwards, on Linux: fsck.exfat must find it clean, and an
# exFAT reader of its own (tools/exfat_check.py) checks the contents.
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
GEARS="$ROOT/sdk/examples/gears/gears.bpk"

command -v mkfs.exfat >/dev/null && command -v fsck.exfat >/dev/null || { echo "needs exfatprogs (mkfs.exfat, fsck.exfat)"; exit 1; }

rm -f "$IMG"
truncate -s 64M "$IMG"
mkfs.exfat -L BANANA "$IMG" >/dev/null || exit 1

{
    echo "mount ## exFAT"
    echo "df ## exfat"
    echo "echo hello exfat > /mnt/virtio/hello.txt"
    echo "cat /mnt/virtio/hello.txt ## hello exfat"
    echo "echo more >> /mnt/virtio/hello.txt"
    echo "mkdir /mnt/virtio/folder"
    echo "cp ~/Examples/gears.bpk /mnt/virtio/folder/gears.bpk"
    echo "mv /mnt/virtio/hello.txt /mnt/virtio/folder/héllo.txt"
    echo "mkdir /mnt/virtio/many"
    for i in $(seq -w 1 60); do echo "echo file $i > /mnt/virtio/many/f$i.txt"; done
    echo "echo gone > /mnt/virtio/many/delete-me.txt"
    echo "rm /mnt/virtio/many/delete-me.txt"
    echo "umount /mnt/virtio"
    echo "mount -a"
    echo "#wait 3"
    echo "ls /mnt/virtio/folder ## gears.bpk"
    echo "cat /mnt/virtio/folder/héllo.txt ## more"
    echo "cat /mnt/virtio/many/f60.txt ## file 60"
    echo "wc /mnt/virtio/folder/gears.bpk"
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
echo "== On Linux: the files"
python3 -I "$ROOT/tools/exfat_check.py" "$IMG" list | grep -v '/many/f[0-5]' || rc=1
n=$(python3 -I "$ROOT/tools/exfat_check.py" "$IMG" list | grep -c '/many/f')
echo "files in /many: $n (want 60)"; [ "$n" = 60 ] || rc=1
python3 -I "$ROOT/tools/exfat_check.py" "$IMG" cat /folder/héllo.txt | tee /dev/stderr | grep -q more || rc=1
python3 -I "$ROOT/tools/exfat_check.py" "$IMG" cmp /folder/gears.bpk "$GEARS" || rc=1
[ $rc -eq 0 ] && echo "== exFAT: everything checks out" || echo "== exFAT: something failed (log: build/logs/exfat.log)"
exit $rc
