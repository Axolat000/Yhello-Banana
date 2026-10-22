#!/bin/sh
# Makes the expected outputs (*.out) of the tests here with Linux's bash
# and zsh (5.2, 5.9: the versions Banana OS has), in the environment
# shelltest gives them on Banana OS.
cd "$(dirname "$0")" || exit 1
for f in b*.sh z*.zsh; do
    case $f in *.zsh) sh="zsh -f" ;; *) sh=bash ;; esac
    (cd /tmp && env -i HOME=/home/banana PATH=/usr/bin:/bin SHELL=/bin/bash TMPDIR=/tmp LANG=C.UTF-8 \
        $sh "$OLDPWD/$f" < /dev/null > "$OLDPWD/${f%.*}.out" 2>&1)
    echo "$f: exit $?"
done
