#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# ncurses 6.5 for Banana OS (MIT licence): the library less and zsh are
# built with (static, wide characters: build/x86_64/lib/libncursesw.a,
# headers in build/x86_64/include), its programs - tput (the package's
# own), clear, infocmp, tic, toe, tabs, tset, reset - and a terminfo
# database for the common terminals, unpacked into /usr/share/terminfo.
#     ports/ncurses/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch ncurses 6.5 136d91bc269a9a5785e5f9e980bc76ab57428f604ce3e5a5a90cebc767971cc6 \
    https://ftp.gnu.org/gnu/ncurses/ncurses-6.5.tar.gz

port_configure --without-shared --with-normal --without-debug --without-ada --without-cxx --without-cxx-binding \
    --without-manpages --without-tests --enable-widec --enable-pc-files=no --with-build-cc=gcc \
    --with-default-terminfo-dir=/usr/share/terminfo --with-terminfo-dirs=/usr/share/terminfo \
    --disable-db-install --disable-home-terminfo --prefix=/usr --bindir=/bin
port_make libs
port_make -C progs

# the terminals' descriptions, compiled by the build machine's tic
# (ncurses 6's format either way) from this version's source
rm -rf terminfo terminfo.zip
mkdir terminfo
tic -x -o terminfo -e xterm-256color,xterm,xterm-color,xterm-16color,linux,vt100,vt102,vt220,screen,screen-256color,tmux,tmux-256color,dumb,ansi,rxvt,rxvt-unicode,putty,cons25 \
    "$SRC/misc/terminfo.src" 2>/dev/null
[ -f terminfo/x/xterm-256color ] || { echo "ncurses: tic made no xterm-256color"; exit 1; }
python3 -c "
import os, sys, zipfile
z = zipfile.ZipFile('terminfo.zip', 'w', zipfile.ZIP_DEFLATED)
for root, dirs, files in sorted(os.walk('terminfo')):
    for f in sorted(files):
        p = os.path.join(root, f)
        z.write(p, os.path.relpath(p, 'terminfo'))
z.close()"
cp progs/tset reset.prog

port_package "$OUT" name=ncurses "title=ncurses" version=$PORT_VER type=console \
    "description=Terminal descriptions (/usr/share/terminfo) and tput, clear, tset, reset, infocmp, tic" \
    "author=Thomas E. Dickey, Free Software Foundation" "category=Developer Tools" \
    bin=tput,clear,infocmp,tic,toe,tabs,tset,reset unpack=terminfo.zip:/usr/share/terminfo \
    -- progs/tput clear=progs/clear infocmp=progs/infocmp tic=progs/tic toe=progs/toe tabs=progs/tabs \
    tset=progs/tset reset=reset.prog terminfo.zip
