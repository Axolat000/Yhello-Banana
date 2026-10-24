#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
gcc -O1 -g -fno-pie -no-pie -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -fsanitize=address,undefined -DBANANA_USER_WORKER \
    -I web/test/shim -I web -I kernel \
    tools/tests/github_reader.c apps/webworker/github.c web/html.c web/arena.c web/page.c \
    kernel/kstring.c -o "$out/test"
"$out/test" "$@"
