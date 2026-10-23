#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU nano 8.7.1 for Banana OS (/bin/nano), on ncurses (ports/ncurses: built
# first), with /apps/nano/nanorc (its sysconfdir) and the syntax colouring
# files (syntax.zip, unpacked into /usr/share/nano). An optional package:
# built into the 64-bit system, installed by `apt install nano`.
#     ports/nano/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch nano 8.7.1 76f0dcb248f2e2f1251d4ecd20fd30fb400a360a3a37c6c340e0a52c2d1cdedf \
    https://www.nano-editor.org/dist/v8/nano-8.7.1.tar.xz
NC=$PORT_DIR/../ncurses/build/x86_64
[ -f "$NC/lib/libncursesw.a" ] || { echo "nano: build ports/ncurses first"; exit 1; }
CPPFLAGS="-I$NC/include"
LDFLAGS="-L$NC/lib"
# (else configure asks the build machine's pkg-config, which adds its -ltinfo)
NCURSESW_CFLAGS="-I$NC/include"
NCURSESW_LIBS="-lncursesw"
PORT_ENV="CPPFLAGS LDFLAGS NCURSESW_CFLAGS NCURSESW_LIBS"
port_configure --enable-utf8 --disable-nls --disable-speller --sysconfdir=/apps/nano --prefix=/usr --bindir=/bin
port_make
rm -f syntax.zip
python3 -c "
import glob, os, zipfile
z = zipfile.ZipFile('syntax.zip', 'w', zipfile.ZIP_DEFLATED)
for p in sorted(glob.glob('$SRC/syntax/*.nanorc')):
    z.write(p, os.path.basename(p))
z.close()"
port_package "$OUT" name=nano "title=GNU nano" version=$PORT_VER type=console \
    "description=GNU nano: a small, friendly text editor for the terminal (/bin/nano)" \
    "author=Chris Allegretta, Benno Schulenberg and the nano developers" "category=Developer Tools" bin=nano \
    unpack=syntax.zip:/usr/share/nano \
    -- src/nano "nanorc=$PORT_DIR/nanorc" "syntax.zip=$B/syntax.zip"
