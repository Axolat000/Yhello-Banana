#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
gcc -O1 -g -fno-pie -no-pie -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -fsanitize=address,undefined -I kernel tools/tests/web_memory.c -o "$out/test"
"$out/test"
