#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Test installation, standalone disk boot, and update on disposable disk images.
# Runs headless without network; preserves the built ISO and cleans up disposable images.
set -euo pipefail
cd "$(dirname "$0")/.."

firmware="${1:-bios}"
base_iso="${BASE_ISO:-Banana_OS.iso}"
test -r "$base_iso"
logs="build/logs/install-$firmware"
mkdir -p "$logs"

# Firmware-specific QEMU arguments
extra_qemu=()
case "$firmware" in
    bios) ;;
    uefi)
        code="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
        vars="${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
        test -r "$code"
        cp "$vars" "$logs/OVMF_VARS.fd"
        extra_qemu=(-drive "if=pflash,format=raw,readonly=on,file=$code"
                    -drive "if=pflash,format=raw,file=$logs/OVMF_VARS.fd")
        ;;
    *) echo "Unknown firmware: $firmware (bios or uefi)" >&2; exit 2 ;;
esac

disk_img="$logs/disposable-disk.img"
rm -f "$disk_img"
qemu-img create -f raw "$disk_img" 2G >/dev/null

cleanup() {
    rm -f "$disk_img"
}
trap cleanup EXIT

base_qemu=(-accel tcg -m 512M -smp 4 -nic none "${extra_qemu[@]}")

{
    printf 'commit=%s\nfirmware=%s\n' "$(git rev-parse HEAD)" "$firmware"
    sha256sum Banana_OS.iso "$base_iso"
    qemu-system-x86_64 --version
    printf 'Base QEMU arguments:'
    printf ' %q' "${base_qemu[@]}"
    printf '\n'
} > "$logs/provenance.txt"

echo "== 1. Installing Banana OS to disposable disk image ($firmware)"
python3 tools/serialtest.py --log "$logs/serial-install.log" \
    --boot-timeout 180 --timeout 180 \
    --cmd 'disks' \
    --cmd 'install --minimal ##? Continue? [y/N]' \
    --cmd 'y ## install: done' \
    --cmd 'echo banana-ci-persistence > /home/banana/ci-persist.txt' \
    --cmd 'sync' \
    -- "${base_qemu[@]}" -cdrom "$base_iso" -boot d \
       -drive "file=$disk_img,format=raw,if=ide" \
    2>&1 | tee "$logs/results-install.log"

echo "== 2. Booting standalone from installed disk ($firmware)"
python3 tools/serialtest.py --log "$logs/serial-diskboot.log" \
    --boot-timeout 180 --timeout 180 \
    --cmd 'cat /home/banana/ci-persist.txt ## banana-ci-persistence' \
    -- "${base_qemu[@]}" \
       -drive "file=$disk_img,format=raw,if=ide" \
    2>&1 | tee "$logs/results-diskboot.log"

echo "== 3. Updating from ISO onto installed disk ($firmware)"
if [ "$firmware" = "uefi" ]; then
    # Refresh disposable vars so UEFI boots from the CD rather than the installed disk
    cp "$vars" "$logs/OVMF_VARS.fd"
fi
if cmp -s "$base_iso" Banana_OS.iso; then
    update_cmds=(--cmd 'update ## already the same')
else
    update_cmds=(--cmd 'update ##? kept. [y/N]' --cmd 'y ## update: done')
fi
python3 tools/serialtest.py --log "$logs/serial-update.log" \
    --boot-timeout 180 --timeout 180 \
    "${update_cmds[@]}" \
    -- "${base_qemu[@]}" -cdrom Banana_OS.iso -boot d \
       -drive "file=$disk_img,format=raw,if=ide" \
    2>&1 | tee "$logs/results-update.log"

echo "== 4. Boot updated disk again and verify user data"
python3 tools/serialtest.py --log "$logs/serial-after-update.log" \
    --boot-timeout 180 --timeout 180 \
    --cmd 'selftest boot ## 0 failed' \
    --cmd 'pkg info terminal ## Terminal' \
    --cmd 'pkg remove terminal ## integrated OS component' \
    --cmd 'pkg remove bash ## integrated OS component' \
    --cmd 'pkg remove zsh ## integrated OS component' \
    --cmd 'cat /home/banana/ci-persist.txt ## banana-ci-persistence' \
    -- "${base_qemu[@]}" -drive "file=$disk_img,format=raw,if=ide" \
    2>&1 | tee "$logs/results-after-update.log"

echo "== All installation, disk boot, and update checks passed on $firmware"
