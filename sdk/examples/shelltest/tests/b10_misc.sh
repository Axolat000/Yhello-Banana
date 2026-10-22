umask 022; umask
cd /tmp; cd /; cd - >/dev/null; pwd
pushd /tmp >/dev/null; pushd / >/dev/null; dirs | wc -w; popd >/dev/null; pwd; popd >/dev/null
type -t echo cd ls | tr '\n' ' '; echo
command -v sed >/dev/null && echo cmd-v
hash -r; echo hashed
set -u; (echo ${nope}) 2>/dev/null || echo nounset-ok; set +u
shopt -q extglob || echo extglob-off
echo "${BASH_VERSINFO[0]}"
[[ $- == *h* ]] && echo h-on
ulimit -n >/dev/null && echo ulimit-ok
times >/dev/null && echo times-ok
x=$(printf 'a\0b' | wc -c); echo $x
IFS=, ; set -- $(echo "a,b,c"); unset IFS; echo $#
printf '%s\n' "$(cat /etc/hostname)" | grep -q . && echo hostname-file
echo $SHELL | grep -q bash && echo shell-bash
[ -t 0 ] || echo not-a-tty
id -u >/dev/null && echo id-ok
whoami | grep -q . && echo whoami-ok
