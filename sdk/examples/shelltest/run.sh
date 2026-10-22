#!/bin/bash
# shelltest's runner (on Banana OS: /apps/shelltest/run.sh): every test
# script here run by bash (b*.sh) or zsh -f (z*.zsh), in the environment
# tests/gen.sh made the expected output in on Linux, compared byte for byte.
#   shelltest [name...]       (a part of a name: only those tests)
here=$(dirname "$0")
pass=0 fail=0
for t in "$here"/b*.sh "$here"/z*.zsh; do
    n=$(basename "$t"); n=${n%.*}
    if [ $# -gt 0 ]; then
        want=0; for a; do [[ $n == *"$a"* ]] && want=1; done
        [ $want = 1 ] || continue
    fi
    case $t in *.zsh) sh="/bin/zsh -f" ;; *) sh=/bin/bash ;; esac
    got=$(cd /tmp && env -i HOME=/home/banana PATH=/usr/bin:/bin SHELL=/bin/bash TMPDIR=/tmp LANG=C.UTF-8 \
        timeout 60 $sh "$t" < /dev/null 2>&1; echo "~$?")
    code=${got##*~}; got=${got%~*}
    if [ "$got" == "$(cat "$here/$n.out")"$'\n' ] || [ "$got" == "$(cat "$here/$n.out")" ]; then
        echo "  ok    $n"
        pass=$((pass + 1))
    else
        echo "  FAIL  $n (exit $code) - expected (<) and got (>):"
        printf '%s' "$got" > /tmp/shelltest.got     # (files: the report must not need <(...) itself)
        diff "$here/$n.out" /tmp/shelltest.got | head -24 | sed 's/^/          /'
        rm -f /tmp/shelltest.got
        fail=$((fail + 1))
    fi
done
echo "shelltest: $pass passed, $fail failed"
exit $fail
