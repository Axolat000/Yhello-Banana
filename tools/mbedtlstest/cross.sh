#!/bin/sh
# Build the port with the kernel's real architecture flags, without app rebuilds.
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d /tmp/banana-mbedtls-cross.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
for suffix in o o64; do
    objects=$(printf '%s\n' ports/mbedtls/*.c third_party/mbedtls/library/*.c | sed "s/\.c$/.$suffix/")
    # Source/object names in these directories contain no whitespace.
    make -j6 $objects kernel/timer.$suffix
    case "$suffix" in
        o) format=elf_i386 ;;
        o64) format=elf_x86_64 ;;
    esac
    ld -m "$format" -r -o "$work/port.$suffix" $objects
    nm -u "$work/port.$suffix" > "$work/undefined"
    python3 - "$work/undefined" <<'PY'
import pathlib, sys
allowed = {
    'kzalloc', 'kfree', 'ksnprintf', 'kvsnprintf',
    'memcpy', 'memmove', 'memset', 'memcmp', 'strlen', 'strcmp', 'strncmp',
    'strchr', 'strrchr', 'strstr',
    'rtc_now_utc', 'rtc_split_time', 'rtc_make_time', 'timer_ms64',
    'tcp_send', 'tcp_recv',
    '__udivdi3', '__umoddi3', '__udivmoddi4', '__divdi3', '__moddi3',
    '__udivti3',
}
actual = {line.split()[-1] for line in pathlib.Path(sys.argv[1]).read_text().splitlines()}
unexpected = actual - allowed
if unexpected:
    raise SystemExit('Unexpected runtime dependencies: ' + ', '.join(sorted(unexpected)))
print('PASS: freestanding port link; only kernel/libgcc dependencies remain')
PY
done
