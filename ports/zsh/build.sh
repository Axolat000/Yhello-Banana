#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# zsh 5.9 for Banana OS (/bin/zsh), its modules built in, on ncurses
# (ports/ncurses: built first), with /apps/zsh/zshrc (prompt, history,
# completion) and its function library - completion, prompt themes, vcs_info
# (functions.zip, unpacked into /usr/share/zsh: its fpath). Left out: the
# completions for other systems (AIX, BSD, Cygwin, macOS, Solaris and other
# Linux distributions' tools).
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
# the function library, as `make install.fns` lays it out (one folder)
rm -rf fns functions.zip
make install.fns DESTDIR="$B/fns" >/dev/null
for d in AIX BSD Cygwin Darwin Solaris Mandriva Redhat openSUSE Debian; do
    for f in "$SRC"/Completion/$d/*/*; do rm -f "fns/usr/share/zsh/$PORT_VER/functions/${f##*/}"; done
done
# compinit's dump of all of it, made here by the build machine's zsh (the
# same 5.9: the dump is version-tagged) - so a first zsh needs not scan the
# 1100 functions on Banana OS (3 s); the zshrc copies it to ~/.zcompdump
F=$B/fns/usr/share/zsh
if command -v zsh >/dev/null && zsh --version | grep -q "zsh $PORT_VER "; then
    mkdir -p "$F/site-functions"
    zsh -f -c "fpath=($F/site-functions $F/$PORT_VER/functions); autoload -Uz compinit; compinit -u -d $F/zcompdump" \
        && sed -i "s|$F|/usr/share/zsh|g" "$F/zcompdump"
else
    echo "zsh: no zsh $PORT_VER on the build machine - no prebuilt completion dump (a first zsh is slower)"
fi
python3 -c "
import os, zipfile
z = zipfile.ZipFile('functions.zip', 'w', zipfile.ZIP_DEFLATED)
top = 'fns/usr/share/zsh'
for root, dirs, files in sorted(os.walk(top)):
    dirs.sort()
    for f in sorted(files):
        p = os.path.join(root, f)
        z.write(p, os.path.relpath(p, top))
z.close()"
port_package "$OUT" name=zsh "title=Z shell" version=$PORT_VER type=console \
    "description=zsh, the Z shell: a bash-like shell with more (/bin/zsh)" \
    "author=Paul Falstad and the zsh developers" "category=Developer Tools" bin=zsh unpack=functions.zip:/usr/share/zsh \
    -- Src/zsh "zshrc=$PORT_DIR/zshrc" "functions.zip=$B/functions.zip"
