#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# kernel/ext4.c built for Linux (stub/: the kernel's helpers as libc) and
# checked against Linux's own e2fsprogs: a filesystem made and used, then
# e2fsck -fn on it; and power cuts in the middle of a flush - replayed by
# Banana OS's mount, by e2fsck, or (before the commit block) not at all.
# Needs gcc and e2fsprogs (e2fsck, dumpe2fs). Usage: sh run.sh [MB]
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
cp ../../kernel/ext4.c ../../kernel/ext4.h stub/*.h "$T"/
gcc -O1 -g -fsanitize=address,undefined -Wall -Wno-unused -I "$T" -o "$T/host" host.c "$T/ext4.c"
gcc -O1 -g -fsanitize=address,undefined -Wall -Wno-unused -I "$T" -o "$T/crash" crash.c "$T/ext4.c"
E2FSCK=$(command -v e2fsck || echo /sbin/e2fsck)
echo "== made, used, mounted again (${1:-300} MB)"
"$T/host" "$T/fs.img" "${1:-300}"
"$E2FSCK" -fn "$T/fs.img"
echo "== a cut after the commit block: replayed by the mount"
"$T/crash" "$T/c.img" 0 && "$T/crash" "$T/c.img" 1 && "$E2FSCK" -fn "$T/c.img"
echo "== the same, replayed by e2fsck"
"$T/crash" "$T/c.img" 0 && "$E2FSCK" -fy "$T/c.img" >/dev/null || true
"$E2FSCK" -fn "$T/c.img" && "$T/crash" "$T/c.img" 1
echo "== a cut before the commit block: as if it never began"
PRE=1 "$T/crash" "$T/c.img" 0 && "$E2FSCK" -fn "$T/c.img"
rm -rf "$T"
echo "ext4test: all passed"
# (addfiles.c: N files put into an installed disk image, for tests of big trees -
#  gcc -I "$T" addfiles.c "$T/ext4.c"; ./a.out disk.img 20000)
