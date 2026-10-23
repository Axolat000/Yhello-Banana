#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
gcc -O2 -g -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -fsanitize=address,undefined --param asan-globals=0 \
    -I kernel -I shell -I net -I crypto -I usb -I web -I media \
    tools/tests/fsdisk_boot.c kernel/crc32c.c -o "$out/test"
"$out/test"
