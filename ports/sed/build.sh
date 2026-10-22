#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU sed 4.9 for Banana OS (/bin/sed).   ports/sed/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch sed 4.9 6e226b732e1cd739464ad6862bd1a1aba42d7982922da7a53519631d24975181 \
    https://ftp.gnu.org/gnu/sed/sed-4.9.tar.xz
port_configure --disable-nls --disable-acl --without-selinux --prefix=/usr --bindir=/bin
port_make
port_package "$OUT" name=sed "title=GNU sed" version=$PORT_VER type=console \
    "description=The stream editor: edits text as it flows through (/bin/sed)" \
    "author=Free Software Foundation" "category=Developer Tools" bin=sed -- sed/sed
