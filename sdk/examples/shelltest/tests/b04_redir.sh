d=$(mktemp -d); cd "$d"
echo one > f; echo two >> f; cat f
echo err 1>&2 2>/dev/null; ls nonexist 2>/dev/null || echo "ls failed $?"
{ echo out; echo err >&2; } > o 2> e; cat o e
{ echo out; echo err >&2; } > both 2>&1; sort both
cat < f | tr a-z A-Z
cat <<EOF
here $HOME_not_set ${#HOME} line
EOF
cat <<'EOF'
literal $x
EOF
cat <<-EOF
	tabs stripped
	EOF
tr a-z A-Z <<< "here string"
exec 3> fd3; echo via-fd3 >&3; exec 3>&-; cat fd3
exec 4< f; read -r l1 <&4; read -r l2 <&4; exec 4<&-; echo "$l1-$l2"
diff <(echo a) <(echo a) && echo procsub-same
paste <(printf '1\n2\n') <(printf 'x\ny\n')
echo data > >(cat > ps.txt); sleep 1; cat ps.txt
set -o noclobber; (echo x > f) 2>/dev/null || echo noclobber-ok; echo y >| f; set +o noclobber; cat f
echo abc > rw; exec 5<>rw; read -n 1 c <&5; echo X >&5; exec 5>&-; cat rw; echo "$c"
ls /dev/fd/ >/dev/null && echo devfd-ok
cmd_out=$(echo multi; echo lines); echo "$cmd_out" | wc -l
echo `echo backtick`
cd /; rm -rf "$d"
