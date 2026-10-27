#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Neatvi: an independent UTF-8 vi/ex editor, without Vim or a Vim runtime.
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch neatvi 26e9cadbf3828807a35bbe71f19e323e97b27439 d72b3dadb723ca7dbe03f1b03404fd47fc5164d42bad310d471c868b79e8f71e \
    https://github.com/aligrudi/neatvi/archive/26e9cadbf3828807a35bbe71f19e323e97b27439.tar.gz
mkdir -p "$B"
cp "$SRC"/*.[ch] "$SRC/Makefile" "$B/"
cd "$B"
# Ship the upstream ISC notice with the binary.
awk '1; /^ [*][/]/ {exit}' "$SRC/vi.c" > LICENSE
make -j"$(getconf _NPROCESSORS_ONLN)" CC="$SDK/tools/banana-cc" CFLAGS="-O2 -g0" LDFLAGS="" vi
port_package "$OUT" name=vi "title=vi (Neatvi)" version=2026.10.27 type=console \
    "description=Independent UTF-8 vi/ex editor (Neatvi), separate from Vim" \
    "author=Ali Gholami Rudi" "category=Developer Tools" bin=vi,ex \
    -- vi "README=$SRC/README" "LICENSE=$B/LICENSE"
