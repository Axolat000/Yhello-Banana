#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU bash 5.2 for Banana OS (64-bit: it needs fork), with its own readline:
#     ports/bash/build.sh <output.bpk>
# makes the package bash, whose program is also /bin/bash and /bin/sh
# (bin= in its manifest, kernel/pkg.c). The source is downloaded once
# (checked against its SHA-256) into ports/bash/src; it is built in
# ports/bash/build/x86_64 with the SDK's cross compiler (sdk/tools/banana-cc).
set -e
OUT=$(realpath -m "$1")
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
SDK=$(cd "$HERE/../../sdk" && pwd)
VER=5.2.37
SHA=9599b22ecd1d5787ad7d3b7bf0c59f312b3396d1e281175dd1f8a4014da621ff
SRC=$HERE/src/bash-$VER
B=$HERE/build/x86_64

mkdir -p "$HERE/build"
exec 9>"$HERE/build/.lock"
flock 9

if [ ! -d "$SRC" ]; then
    mkdir -p "$HERE/src"
    T=$HERE/src/bash-$VER.tar.gz
    [ -f "$T" ] || curl -fsSL -o "$T" https://ftp.gnu.org/gnu/bash/bash-$VER.tar.gz
    echo "$SHA  $T" | sha256sum -c -
    tar -C "$HERE/src" -xzf "$T"
fi

# what configure cannot find out by running a program (cross-compiling),
# as it is on Banana OS
export bash_cv_getcwd_malloc=yes          # getcwd(NULL, 0) allocates (no lib/sh/getcwd.c)
export bash_cv_dev_fd=standard            # /dev/fd/N: process substitution <(...)
export bash_cv_dev_stdin=present
export bash_cv_sys_named_pipes=missing
export bash_cv_job_control_missing=present
export bash_cv_wcontinued_broken=no
export bash_cv_func_sigsetjmp=present
export bash_cv_must_reinstall_sighandlers=no
export bash_cv_sys_siglist=no bash_cv_under_sys_siglist=no
export bash_cv_opendir_not_robust=no
export bash_cv_printf_a_format=yes
export bash_cv_func_snprintf=yes bash_cv_func_vsnprintf=yes
export bash_cv_ulimit_maxfds=no
export bash_cv_unusable_rtsigs=no
export bash_cv_pgrp_pipe=no
export bash_cv_wexitstatus_offset=8
export bash_cv_dup2_broken=no
export bash_cv_fnmatch_equiv_fallback=no

# configured again only when what goes into configure changes (CONFV:
# bump it when changing the options below)
CONFV=1
OPTS="--without-bash-malloc --disable-nls --enable-job-control --with-installed-readline=no --without-curses
 --prefix=/usr --bindir=/bin"
SUM=$( (echo "$CONFV $VER $OPTS"; env | grep '^bash_cv_' | sort; cat "$SDK/tools/banana-configure" "$SDK/tools/banana-config.site") |
      sha256sum | cut -d" " -f1)
mkdir -p "$B"
cd "$B"
if [ ! -f Makefile ] || [ "$(cat .configured 2>/dev/null)" != "$SUM" ]; then
    # shellcheck disable=SC2086
    BANANA_ARCH=x86_64 CFLAGS="-O2 -g0" "$SDK/tools/banana-configure" "$SRC/configure" $OPTS \
        >configure.log 2>&1 || { tail -30 configure.log; exit 1; }
    echo "$SUM" > .configured
fi
# (the objects are made again when the SDK's headers or C library change:
# banana-cc's own cache follows the C library, make does not see headers)
SDKSUM=$(cat "$SDK"/include/*.h "$SDK"/include/*/*.h | sha256sum | cut -d" " -f1)
if [ "$(cat .sdk 2>/dev/null)" != "$SDKSUM" ]; then
    make clean >/dev/null 2>&1 || true
    echo "$SDKSUM" > .sdk
fi
# (bash's Makefile makes pathnames.h twice at once under -j: once first)
make pathnames.h >/dev/null
make -j"$(nproc)" bash >make.log 2>&1 || { grep -E "error|Error" make.log | head -30; exit 1; }
strip -o app-x86_64 bash

cat > manifest <<EOF
name=bash
title=GNU Bash
version=$VER
type=console
description=The GNU Bourne Again shell - /bin/bash and /bin/sh
author=Free Software Foundation
category=Developer Tools
bin=bash,sh
EOF
python3 "$SDK/tools/bpkg" pack -o "$OUT" manifest app-x86_64 >/dev/null
python3 "$SDK/tools/bpkg" check "$OUT"
