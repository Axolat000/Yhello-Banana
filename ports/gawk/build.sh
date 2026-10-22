#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU awk 5.3 for Banana OS (/bin/gawk and /bin/awk).
#     ports/gawk/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch gawk 5.3.1 694db764812a6236423d4ff40ceb7b6c4c441301b72ad502bb5c27e00cd56f78 \
    https://ftp.gnu.org/gnu/gawk/gawk-5.3.1.tar.xz
port_configure --disable-nls --disable-extensions --disable-mpfr --without-readline --without-libsigsegv-prefix \
    --prefix=/usr --bindir=/bin
port_make -C support
port_make gawk
port_package "$OUT" name=gawk "title=GNU awk" version=$PORT_VER type=console \
    "description=The awk language: patterns and actions over text (/bin/awk, /bin/gawk)" \
    "author=Free Software Foundation" "category=Developer Tools" bin=gawk,awk -- gawk
