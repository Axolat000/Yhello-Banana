#!/usr/bin/env bash
# Banana OS development helper: build, boot in QEMU, and keep the logs.
#
#   tools/dev.sh build            build the ISO (make), output also in build/logs/build.log
#   tools/dev.sh run  [bios|uefi|32]   build, then boot in a QEMU window;
#                                 the serial console goes to the terminal and to
#                                 build/logs/serial-<firmware>.log
#   tools/dev.sh test [bios|uefi|32] [seconds]
#                                 build, then boot without a window for a while
#                                 (default 60 s) and check the serial log for a panic
#   tools/dev.sh apps [bios|uefi|32]
#                                 build, boot without a window and run
#                                 `selftest` and the threads / cltest examples
#                                 at the serial console; log in
#                                 build/logs/apps-<firmware>.log
#   tools/dev.sh clean            make clean
#
# Environment: MEM (QEMU memory, default 512M), SMP (cores, default 4),
# NIC (QEMU NIC model, default e1000), OVMF (UEFI firmware file),
# DISK (a disk image attached as a virtio disk: mounted at /mnt/virtio),
# CPU (QEMU's processor model: CPU=max has AVX / AVX2 / AVX-512),
# GPU (std, virtio or vmware; default std),
# KVM=1 (hardware virtualization: much faster, your processor's own features).

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOGS="$ROOT/build/logs"
mkdir -p "$LOGS"

MEM="${MEM:-512M}"
SMP="${SMP:-4}"
NIC="${NIC:-e1000}"

red()   { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }

check_tools() {
    local missing=()
    for t in nasm gcc ld objcopy xorriso mformat mcopy mmd python3 qemu-system-x86_64; do
        command -v "$t" >/dev/null 2>&1 || missing+=("$t")
    done
    if ! echo 'int main(void){return 0;}' | gcc -m32 -x c - -o /dev/null 2>/dev/null; then
        missing+=("gcc -m32 (multilib)")
    fi
    if [ ${#missing[@]} -ne 0 ]; then
        red "Missing: ${missing[*]}"
        echo "  Arch:   sudo pacman -S nasm gcc binutils libisoburn mtools python qemu-full lib32-glibc lib32-gcc-libs edk2-ovmf"
        echo "  Debian: sudo apt install nasm gcc-multilib binutils xorriso mtools python3 qemu-system-x86 ovmf"
        exit 1
    fi
}

find_ovmf() {
    if [ -n "${OVMF:-}" ]; then echo "$OVMF"; return; fi
    for f in /usr/share/edk2/x64/OVMF.4m.fd /usr/share/edk2-ovmf/x64/OVMF.4m.fd \
             /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF.fd /usr/share/qemu/OVMF.fd \
             /usr/share/edk2-armvirt/x64/OVMF.4m.fd; do
        [ -f "$f" ] && { echo "$f"; return; }
    done
}

do_build() {
    check_tools
    bold "== Building ($(nproc) jobs) - log: build/logs/build.log"
    local start=$SECONDS
    (cd "$ROOT" && make -j"$(nproc)") 2>&1 | tee "$LOGS/build.log"
    local rc=${PIPESTATUS[0]}
    local warnings
    warnings=$(grep -c 'warning:' "$LOGS/build.log" || true)
    if [ "$rc" -ne 0 ]; then
        red "== Build FAILED (exit $rc) after $((SECONDS - start)) s. Errors:"
        grep -n -i 'error\|\*\*\*' "$LOGS/build.log" | head -30
        exit "$rc"
    fi
    green "== Build OK in $((SECONDS - start)) s ($warnings warnings)"
}

# qemu_args <firmware>: sets QEMU_BIN and QEMU_ARGS for that firmware
qemu_args() {
    local fw="$1"
    local qemu=qemu-system-x86_64
    local args=(-cdrom "$ROOT/Banana_OS.iso" -m "$MEM" -smp "$SMP" -nic "user,model=$NIC")
    case "$fw" in
        bios) ;;
        uefi)
            local ovmf; ovmf="$(find_ovmf)"
            [ -n "$ovmf" ] || { red "OVMF not found (install edk2-ovmf / ovmf, or set OVMF=...)"; exit 1; }
            args+=(-bios "$ovmf") ;;
        32)   qemu=qemu-system-i386; args=(-cdrom "$ROOT/Banana_OS.iso" -m "$MEM" -nic "user,model=$NIC") ;;
        *)    red "unknown firmware '$fw' (bios, uefi or 32)"; exit 1 ;;
    esac
    case "${GPU:-std}" in
        std|virtio|vmware) args+=(-vga "${GPU:-std}") ;;
        *) red "unknown GPU '${GPU}' (std, virtio or vmware)"; exit 1 ;;
    esac
    # (-boot d: the CD first - the firmware would try the disk before it)
    if [ -n "${DISK:-}" ]; then args+=(-drive "file=$DISK,format=raw,if=virtio" -boot d); fi
    if [ "${KVM:-0}" = 1 ]; then args+=(-enable-kvm -cpu "${CPU:-host}")
    elif [ -n "${CPU:-}" ]; then args+=(-cpu "$CPU"); fi
    QEMU_BIN="$qemu"
    QEMU_ARGS=("${args[@]}")
}

do_run() {
    local fw="${1:-bios}"
    do_build
    qemu_args "$fw"
    local log="$LOGS/serial-$fw.log"
    bold "== Booting ($fw, $MEM, $SMP cores) - serial log: build/logs/serial-$fw.log"
    "$QEMU_BIN" "${QEMU_ARGS[@]}" -serial stdio 2>&1 | tee "$log"
}

do_test() {
    local fw="${1:-bios}" secs="${2:-60}"
    do_build
    qemu_args "$fw"
    local log="$LOGS/serial-$fw.log"
    bold "== Booting headless ($fw) for $secs s - serial log: build/logs/serial-$fw.log"
    timeout "$secs" "$QEMU_BIN" "${QEMU_ARGS[@]}" -display none -serial "file:$log" -no-reboot
    local lines; lines=$(wc -l < "$log")
    if grep -q 'PANIC' "$log"; then
        red "== PANIC during boot:"
        grep -n -B5 -A10 'PANIC' "$log" | head -40
        exit 1
    fi
    if [ "$lines" -eq 0 ]; then
        red "== No serial output at all (the kernel did not start?)"
        exit 1
    fi
    green "== No panic in $secs s ($lines lines of serial output). Last lines:"
    tail -15 "$log"
}

do_apps() {
    local fw="${1:-bios}"
    do_build
    qemu_args "$fw"
    bold "== Running apps over the serial console ($fw) - log: build/logs/apps-$fw.log"
    python3 "$ROOT/tools/serialtest.py" --qemu "$QEMU_BIN" --log "$LOGS/apps-$fw.log" -- "${QEMU_ARGS[@]}"
    local rc=$?
    if [ $rc -eq 0 ]; then green "== Every app ran"; else red "== Something failed (see above and build/logs/apps-$fw.log)"; fi
    return $rc
}

case "${1:-build}" in
    build) do_build ;;
    run)   do_run "${2:-bios}" ;;
    test)  do_test "${2:-bios}" "${3:-60}" ;;
    apps)  do_apps "${2:-bios}" ;;
    clean) (cd "$ROOT" && make clean) ;;
    *)     sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
