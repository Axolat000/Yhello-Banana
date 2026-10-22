#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU grep 3.11 for Banana OS (/bin/grep; regular expressions: POSIX basic,
# extended, fixed strings - no Perl ones).   ports/grep/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch grep 3.11 1db2aedde89d0dea42b16d9528f894c8d15dae4e190b59aecc78f5a951276eab \
    https://ftp.gnu.org/gnu/grep/grep-3.11.tar.xz
port_configure --disable-nls --disable-perl-regexp --prefix=/usr --bindir=/bin
port_make
port_package "$OUT" name=grep "title=GNU grep" version=$PORT_VER type=console \
    "description=Searches files for lines that match a pattern (/bin/grep)" \
    "author=Free Software Foundation" "category=Developer Tools" bin=grep -- src/grep
