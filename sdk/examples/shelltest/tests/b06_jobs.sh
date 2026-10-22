sleep 0.2 & p=$!
wait $p; echo "wait $?"
(exit 5) & wait $!; echo "status $?"
for i in 1 2 3; do (sleep 0.$i; echo job$i) & done; wait; echo all-done
sleep 5 & k=$!; kill $k; wait $k; echo "killed $?"
x=$( (sleep 0.1; echo late) & wait ); echo "$x"
coproc C { read l; echo "got $l"; }; echo hi >&"${C[1]}"; read r <&"${C[0]}"; echo "$r"; wait
timeout 1 sleep 3; echo "timeout $?"
echo $$ | grep -q '^[0-9]*$' && echo pid-ok
[ "$PPID" -gt 0 ] && echo ppid-ok
