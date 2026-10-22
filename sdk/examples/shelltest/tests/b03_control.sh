for w in a b c; do echo -n "$w"; done; echo
n=0; while [ $n -lt 3 ]; do n=$((n+1)); done; echo $n
until [ $n -eq 0 ]; do n=$((n-1)); done; echo $n
case foo.txt in *.c) echo c;; *.txt|*.md) echo text;; *) echo other;; esac
case X in [a-z]) echo lower;; [A-Z]) echo upper;; esac
if true && ! false; then echo yes; elif false; then echo no; else echo never; fi
for i in 1 2 3 4 5; do [ $i = 2 ] && continue; [ $i = 4 ] && break; echo $i; done
[[ abc == a* ]] && echo glob-match
[[ abc =~ ^a(b)c$ ]] && echo "regex ${BASH_REMATCH[1]}"
[[ -z "" && -n x ]] && echo zn
[[ 10 -gt 9 ]] && echo num
[ "a" \< "b" ] && echo lt
f() { return 3; }; f; echo "status $?"
g() { local x=inner; echo $x; }; x=outer; g; echo $x
r() { [ $1 -le 1 ] && { echo 1; return; }; echo $(( $1 * $(r $(( $1 - 1 ))) )); }; r 5
true | false; echo "pipe $?"; set -o pipefail; true | false; echo "pipefail $?"; set +o pipefail
{ echo grouped; echo two; } | wc -l
(exit 7); echo "subshell $?"
echo $(( $(echo 4) + 1 ))
select_none=1; echo done
