#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
flags="-O2 -ffunction-sections -fdata-sections -I kernel -I shell -I net -I crypto -I usb -I web -I media"
# Use the real visibility policy with a fixed package inventory; filesystem
# enumeration itself is unrelated to which launchers expose console packages.
gcc $flags -Dpkg_list=catalog_real_pkg_list -c kernel/pkg.c -o "$out/pkg.o"
gcc $flags -Wl,--gc-sections -DTEST_STARTMENU tools/tests/desktop_catalog.c kernel/kstring.c "$out/pkg.o" -o "$out/start"
"$out/start"
gcc $flags -Wl,--gc-sections tools/tests/desktop_catalog.c kernel/kstring.c "$out/pkg.o" -o "$out/apps"
"$out/apps"
