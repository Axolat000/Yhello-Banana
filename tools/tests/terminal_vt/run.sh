#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The terminal app's screen (apps/terminal/vt.c) on Linux: escape sequences in,
# cells checked.  tools/tests/terminal_vt/run.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/terminal_vt_test
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$HERE/../../../apps/terminal" \
    "$HERE/vt_test.c" "$HERE/../../../apps/terminal/vt.c" -o "$OUT"
"$OUT"
