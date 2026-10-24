#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
gcc -O1 -g -fno-pie -no-pie -ffunction-sections -fdata-sections -Wl,--gc-sections     -fsanitize=address,undefined --param asan-globals=0     -I kernel -I shell -I net -I web -I media -I crypto -I usb     tools/tests/web_worker_ipc.c kernel/kstring.c -o "$out/test"
"$out/test"
