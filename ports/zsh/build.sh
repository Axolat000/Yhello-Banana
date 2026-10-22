#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# zsh 5.9 for Banana OS (/bin/zsh), its modules built in, on ncurses
# (ports/ncurses: built first), with /apps/zsh/zshrc (prompt, history).
# Not shipped: its function library
# (completion, prompt themes) - zsh runs without it.
#     ports/zsh/build.sh <output.bpk>
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"
port_fetch zsh 5.9 9b8d1ecedd5b5e81fbf1918e876752a7dd948e05c1a0dba10ab863842d45acd5 \
    https://www.zsh.org/pub/old/zsh-5.9.tar.xz
NC=$PORT_DIR/../ncurses/build/x86_64
[ -f "$NC/lib/libncursesw.a" ] || { echo "zsh: build ports/ncurses first"; exit 1; }
CPPFLAGS="-I$NC/include"
LDFLAGS="-L$NC/lib"
# what configure cannot find out by running a program here
zsh_cv_sys_path_dev_fd=yes
zsh_cv_sys_nis=no
zsh_cv_sys_nis_plus=no
zsh_cv_rlim_t_is_longer=no
zsh_cv_type_rlim_t_is_unsigned=yes
zsh_cv_getpwnam_faked=no
zsh_cv_sys_sigsuspend=yes
zsh_cv_sys_elf=yes
zsh_cv_shared_environ=yes
zsh_cv_sys_dynamic_execsyms=no
PORT_ENV="CPPFLAGS LDFLAGS $(set | sed -n 's/^\(zsh_cv_[a-z_0-9]*\)=.*/\1/p' | tr '\n' ' ')"
port_configure --disable-dynamic --enable-multibyte --disable-gdbm --disable-pcre --disable-cap \
    --with-tcsetpgrp --with-term-lib=ncursesw --enable-etcdir=/apps/zsh --disable-zsh-mem \
    --prefix=/usr --bindir=/bin
port_make
port_package "$OUT" name=zsh "title=Z shell" version=$PORT_VER type=console \
    "description=zsh, the Z shell: a bash-like shell with more (/bin/zsh)" \
    "author=Paul Falstad and the zsh developers" "category=Developer Tools" bin=zsh -- Src/zsh "zshrc=$PORT_DIR/zshrc"
