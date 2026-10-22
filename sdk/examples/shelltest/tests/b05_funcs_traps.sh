trap 'echo exit-trap' EXIT
trap 'echo int-trap' INT
kill -INT $$
sleep 0.1
f() { trap 'echo ret-trap' RETURN; echo in-f; }; f
set -e; (false; echo not-here) || echo sub-failed; set +e
err() { echo "err-trap $?"; }; trap err ERR; false; trap - ERR
g() { echo "args:$#"; for a; do echo "[$a]"; done; }; g "a b" c
h() { local IFS=,; echo "$*"; }; h x y z
export EXV=exported; bash -c 'echo $EXV'
unset -f g; type g >/dev/null 2>&1 || echo g-gone
alias ll='echo aliased'; shopt -s expand_aliases; eval ll
source /dev/stdin <<< 'echo sourced'
eval 'e=evaled'; echo $e
echo "$(declare -f h | head -1)"
( trap 'echo term-caught; exit 0' TERM; kill -TERM $BASHPID; sleep 1 )
