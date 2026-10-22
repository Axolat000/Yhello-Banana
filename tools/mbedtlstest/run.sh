#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d /tmp/banana-mbedtlstest.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
# Compile the same civil-date routines used by the kernel, without CMOS IO.
python3 - "$work/rtc_civil.c" <<'PY'
import pathlib, sys
text = pathlib.Path('kernel/rtc.c').read_text()
start = text.index('static int32_t days_from_civil(')
end = text.index('/* ── the system', start)
pathlib.Path(sys.argv[1]).write_text('#include "rtc.h"\n' + text[start:end])
PY
for arch in ${ARCHES:-64}; do
    mkdir "$work/$arch"
    for source in third_party/mbedtls/library/*.c ports/mbedtls/banana_platform.c ports/mbedtls/banana_transport.c ports/mbedtls/banana_ca.c "$work/rtc_civil.c" kernel/kstring.c tools/mbedtlstest/test.c; do
        gcc -m"$arch" -O2 -ffreestanding -nostdinc -fno-builtin -DNDEBUG \
            -I kernel -I net -I ports/mbedtls -I ports/mbedtls/libc \
            -I third_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"banana_config.h"' \
            -Wall -Wextra -Werror -c "$source" -o "$work/$arch/$(basename "$source" .c).o"
    done
    gcc -m"$arch" "$work/$arch/"*.o -o "$work/test$arch"
    "$work/test$arch"
done
