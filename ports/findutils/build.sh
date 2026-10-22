#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU findutils 4.10 for Banana OS: find and xargs (/bin/find, /bin/xargs;
# the package's main program is find, xargs a program of it - bin=).
#     ports/findutils/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch findutils 4.10.0 1387e0b67ff247d2abde998f90dfbf70c1491391a59ddfecb8ae698789f0a4f5 \
    https://ftp.gnu.org/gnu/findutils/findutils-4.10.0.tar.xz
port_configure --disable-nls --without-selinux --prefix=/usr --bindir=/bin
port_make -C gl
port_make -C lib
port_make -C find find
port_make -C xargs xargs
port_package "$OUT" name=findutils "title=GNU findutils" version=$PORT_VER type=console \
    "description=find (looks for files by name, kind, size, date...) and xargs (/bin)" \
    "author=Free Software Foundation" "category=Developer Tools" bin=find,xargs -- find/find xargs=xargs/xargs
