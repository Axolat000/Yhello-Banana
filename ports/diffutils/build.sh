#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU diffutils 3.10 for Banana OS: diff, cmp, sdiff, diff3 (/bin).
#     ports/diffutils/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch diffutils 3.10 90e5e93cc724e4ebe12ede80df1634063c7a855692685919bfe60b556c9bd09e \
    https://ftp.gnu.org/gnu/diffutils/diffutils-3.10.tar.xz
# (diff3 and sdiff run diff: where it is)
port_configure --disable-nls --prefix=/usr --bindir=/bin
port_make
port_package "$OUT" name=diffutils "title=GNU diffutils" version=$PORT_VER type=console \
    "description=diff, cmp, sdiff and diff3: the differences between files (/bin)" \
    "author=Free Software Foundation" "category=Developer Tools" bin=diff,cmp,sdiff,diff3 \
    -- src/diff cmp=src/cmp sdiff=src/sdiff diff3=src/diff3
