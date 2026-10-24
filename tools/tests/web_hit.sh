#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
gcc -O1 -g -fno-pie -no-pie -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -fsanitize=address,undefined -DFONT_HOST -DBANANA_USER_WORKER \
    -I web/test/shim -I web -I kernel \
    tools/tests/web_hit.c web/layout.c web/render.c kernel/font.c kernel/ttf.c \
    kernel/font8x8.c kernel/kstring.c -lm -o "$out/test"
"$out/test"
