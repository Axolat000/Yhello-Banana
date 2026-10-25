#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Boot the already-built ISO; never rebuild a different artifact in validation.
set -euo pipefail
cd "$(dirname "$0")/.."

firmware="${1:?usage: bash tools/ci-boot.sh bios|uefi}"
logs="build/logs/boot-$firmware"
mkdir -p "$logs"
args=(-accel tcg -cdrom Banana_OS.iso -boot d -m 512M -smp 4 -nic none)
case "$firmware" in
    bios) ;;
    uefi)
        # Ubuntu 24.04 / Debian ovmf: separate read-only code and writable vars.
        code="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
        vars="${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
        test -r "$code"
        cp "$vars" "$logs/OVMF_VARS.fd"
        args+=(-drive "if=pflash,format=raw,readonly=on,file=$code"
               -drive "if=pflash,format=raw,file=$logs/OVMF_VARS.fd")
        ;;
    *) echo "Unknown firmware: $firmware" >&2; exit 2 ;;
esac

{
    printf 'commit=%s\nfirmware=%s\n' "$(git rev-parse HEAD)" "$firmware"
    sha256sum Banana_OS.iso
    qemu-system-x86_64 --version
    printf 'QEMU arguments:'
    printf ' %q' "${args[@]}"
    printf '\n'
} > "$logs/provenance.txt"

# Reaching a prompt and completing commands catches hangs that a timed boot
# with nonempty serial output would otherwise treat as success. No Internet.
python3 tools/serialtest.py --log "$logs/serial.log" \
    --boot-timeout 180 --timeout 180 \
    --cmd 'selftest boot ## 0 failed' \
    --cmd 'selftest browser libjsdom ## 29 passed, 0 failed' \
    --cmd 'pkg remove libjsworker ## integrated OS component' \
    --cmd 'selftest browser isolation ## 0 failed' \
    --cmd 'selftest ram ## 0 failed' \
    --cmd 'pkg install /home/banana/Examples/shelltest.bpk' \
    --cmd 'apt install neovim' \
    --cmd 'shelltest -e ## shelltest: all passed (0 failed)' -- "${args[@]}" \
    2>&1 | tee "$logs/results.log"
