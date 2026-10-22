#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# less 668 for Banana OS (/bin/less, /bin/more as less), on ncurses
# (ports/ncurses: built first).   ports/less/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch less 668 2819f55564d86d542abbecafd82ff61e819a3eec967faa36cd3e68f1596a44b8 \
    https://www.greenwoodsoftware.com/less/less-668.tar.gz
NC=$PORT_DIR/../ncurses/build/x86_64
[ -f "$NC/lib/libncursesw.a" ] || { echo "less: build ports/ncurses first"; exit 1; }
CPPFLAGS="-I$NC/include"
LDFLAGS="-L$NC/lib"
PORT_ENV="CPPFLAGS LDFLAGS"
port_configure --with-regex=posix --with-editor=vi --prefix=/usr --bindir=/bin
port_make less
port_package "$OUT" name=less "title=less" version=$PORT_VER type=console \
    "description=A pager: reads a file or a pipe a screen at a time, scrolls both ways, searches (/bin/less, /bin/more)" \
    "author=Mark Nudelman" "category=Developer Tools" bin=less,more -- less
