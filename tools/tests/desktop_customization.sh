#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
gcc -D_POSIX_C_SOURCE=200809L -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
 apps/terminal/prefs.c tools/tests/terminal_prefs.c -o "$out/prefs"
"$out/prefs" "$out"
gcc -O1 -g -fno-pie -no-pie -ffunction-sections -fdata-sections \
 -Wl,--gc-sections -fsanitize=address,undefined \
 kernel/winframe.c tools/tests/window_fullscreen.c -o "$out/fullscreen"
"$out/fullscreen"
