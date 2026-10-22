a=(one two three); echo $a[2] ${#a} $a[-1] ${a[2,3]}
typeset -A h; h=(k1 v1 k2 v2); echo ${(k)h} | tr ' ' '\n' | sort | tr '\n' ' '; echo
s="Hello World"; echo ${s:u} ${s:l} ${(U)s} ${#s} ${s[1,5]}
echo ${(s:,:)${:-a,b,c}}; x=(c a b); echo ${(o)x} ${(O)x}
print -l a b; print -r -- '\n literal'; printf '%s\n' ok
for i in {1..3}; do echo -n $i; done; echo
f() { echo "f $# $1"; }; f 1 2
setopt extendedglob; cd ${TMPDIR:-/tmp}; d=zt$$; mkdir $d; cd $d; touch a.txt b.log
echo *.txt; echo ^*.txt; echo *(.); echo **/*(.N) | wc -w; cd ..; rm -rf $d
echo $(( 7 / 2 )) $(( 7.0 / 2 )) $(( 2 ** 8 ))
[[ abc == a* ]] && echo match
case foo in f*) echo fstar;; esac
autoload -Uz add-zsh-hook 2>/dev/null && echo autoload-ok
zmodload zsh/datetime && echo datetime-ok
repeat 2 echo rep
echo ${${:-nested}:u}
local_t() { local v=in; echo $v }; local_t
v=1; (( v++ )); echo $v
echo ${ZSH_VERSION%%.*}
