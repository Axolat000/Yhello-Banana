#!/usr/bin/env bash
# Integration-only checks for the combined contribution branch.
set -euo pipefail
cd "$(dirname "$0")/.."
firmware="${1:?bios|uefi}"
logs="build/logs/native-$firmware"
mkdir -p "$logs"
args=(-accel tcg -m 768M -smp 4 -nic none -cdrom Banana_OS.iso -boot d)
case "$firmware" in
  bios) ;;
  uefi)
    cp /usr/share/OVMF/OVMF_VARS_4M.fd "$logs/OVMF_VARS.fd"
    args+=(-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd
           -drive "if=pflash,format=raw,file=$logs/OVMF_VARS.fd") ;;
  *) exit 2 ;;
esac
python3 tools/serialtest.py --log "$logs/serial.log" --boot-timeout 180 --timeout 180 \
  --cmd '/bin/kcmd latency on ## latency: sample written to serial log' \
  --cmd '/bin/kcmd startx' \
  --cmd '#wait 2' \
  --cmd '/bin/kcmd screenshot /home/banana/Pictures ## Screenshot saved:' \
  --cmd 'ls /home/banana/Pictures ## Screenshot-' \
  --cmd '/bin/kcmd notify CI-ready "Native notification service"' \
  --cmd '#wait 2' \
  --cmd '/bin/kcmd latency off ## latency: sample written to serial log' \
  -- "${args[@]}" 2>&1 | tee "$logs/results.log"
