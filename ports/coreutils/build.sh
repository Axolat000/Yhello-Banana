#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU coreutils 9.5 for Banana OS: ls, cp, mv, rm, cat, mkdir, chmod, sort,
# head, tail, wc, date, du, df, env, test... - one program (coreutils'
# single binary: it is the command its name says) with every command a
# link in /bin (bin= in its manifest).
#     ports/coreutils/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"

port_fetch coreutils 9.5 cd328edeac92f6a665de9f323c93b712af1858bc2e0d88f3f7100469470a1b8a \
    https://ftp.gnu.org/gnu/coreutils/coreutils-9.5.tar.xz

port_configure --enable-single-binary=symlinks --disable-nls --without-selinux --without-openssl \
    --disable-acl --disable-xattr --disable-libcap --without-libgmp --enable-install-program=hostname,arch \
    --prefix=/usr --bindir=/bin
port_make

# the commands: what the single program answers to
# (install is "ginstall" inside: its name outside is install)
PROGS=$(sed -n 's/^SINGLE_BINARY_PROGRAM("\([^"]*\)",.*/\1/p' src/coreutils.h | sed 's/^ginstall$/install/' | sort -u | tr '\n' ',' | sed 's/,$//')
[ -n "$PROGS" ] || { echo "coreutils: no program list"; exit 1; }
port_package "$OUT" name=coreutils "title=GNU coreutils" version=$PORT_VER type=console \
    "description=The GNU core utilities: ls, cp, mv, rm, cat, sort, date and more (in /bin)" \
    "author=Free Software Foundation" "category=Developer Tools" "bin=$PROGS" -- src/coreutils
