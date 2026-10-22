read a b c <<< "1 2 3 4"; echo "$a|$b|$c"
IFS=: read -r x y <<< "u:v:w"; echo "$x|$y"
read -ra arr <<< "p q r"; echo "${#arr[@]} ${arr[2]}"
read -d ';' t <<< "abc;def"; echo "$t"
printf 'line\nline2\n' | while read -r l; do echo "<$l>"; done
mapfile -t lines < <(printf 'a\nb\nc\n'); echo "${#lines[@]} ${lines[1]}"
printf '%5s|%-5s|%05d|%x|%o|%e|%.2f|%c|%%\n' ab cd 42 255 8 1234.5 3.14159 xyz
printf -v pv '%03d' 7; echo $pv
printf '%s\n' "${arr[@]}"
printf '%b\n' 'a\tb'
printf '%q\n' "a b'c"
echo -e 'esc\x21' ; echo -n no-nl; echo
getopts_test() { local OPTIND o; while getopts "ab:c" o; do echo "opt $o ${OPTARG:-}"; done; shift $((OPTIND-1)); echo "rest $*"; }
getopts_test -a -b val -c file
