# variables, quoting, parameter expansion
a=hello b="two words" c=
echo "$a" "$b" "[$c]" ${#b}
echo "${a^^} ${a^} ${b// /_} ${a:1:3} ${a: -2} ${a#he} ${a%lo} ${b##* } ${b%% *}"
echo "${undef:-dflt} ${undef-x} ${c:-empty} ${c-set} ${a:+alt} ${undef:+no}"
: ${newv:=assigned}; echo "$newv"
x=abcabc; echo "${x/b/B} ${x//b/B} ${x/#a/A} ${x/%c/C}"
printf '%s|' 'single $a' "double $a" $'ansi\tTab' "esc \"q\""; echo
arr=(one "two three" four); echo "${#arr[@]} ${arr[1]} ${arr[-1]} ${!arr[@]}"
arr+=(five); arr[7]=seven; echo "${#arr[@]} ${!arr[*]}"
for e in "${arr[@]}"; do printf '<%s>' "$e"; done; echo
unset 'arr[0]'; echo "${arr[*]}"
declare -A m=([k1]=v1 [k2]=v2); m[k3]=v3
for k in $(printf '%s\n' "${!m[@]}" | sort); do echo "$k=${m[$k]}"; done
declare -i n=5; n+=3; echo $n
readonly RO=1; (RO=2) 2>/dev/null || echo "readonly ok"
echo "${arr[@]:1:2}"
set -- p1 "p 2" p3; echo "$# $1 $2 $* ${@:2}"
shift; echo "$1"
f() { echo "$FUNCNAME $# $1"; }; f x y
v=world; ref=v; echo "${!ref}"; declare -n nr=v; nr=changed; echo $v
echo $'\x41\101é'
