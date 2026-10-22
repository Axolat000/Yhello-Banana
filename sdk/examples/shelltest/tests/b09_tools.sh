d=$(mktemp -d); cd "$d"
printf 'c 3\na 1\nb 2\na 1\n' > data
sort data | uniq -c | sed 's/^ *//'
sort -k2 -n data | head -2; sort -u data | wc -l; sort -r data | head -1
cut -d' ' -f1 data | tr '\n' ','; echo
awk '{s+=$2} END {print "sum", s}' data
awk -F' ' '$2>1 {print $1}' data
sed -n '2p' data; sed 's/a/A/g' data | grep -c A; sed -i 's/c/C/' data; head -1 data
grep -n b data; grep -c a data; grep -v a data | wc -l; grep -E '^(a|b)' data | wc -l
grep -o '[0-9]' data | sort -u | tr -d '\n'; echo
mkdir -p x/y/z; touch x/y/z/f1 x/f2; find x -type f | sort; find x -name 'f*' -exec basename {} \; | sort
find x -type f | xargs -n1 basename | sort | tr '\n' ' '; echo
cp data copy; mv copy moved; ln -s moved link; ln moved hard; ls -1 | sort | tr '\n' ' '; echo
stat -c '%h %s' moved; readlink link; [ -L link ] && [ -f hard ] && echo links-ok
chmod 750 moved; stat -c %a moved; [ -x moved ] && echo exec-bit
echo hello | wc -c; wc -l < data; echo 'a b  c' | tr -s ' '; echo abc | rev 2>/dev/null || echo abc | awk '{for(i=length;i;i--) printf substr($0,i,1); print ""}'
seq 3 | paste -sd+ | bc 2>/dev/null || seq 3 | awk '{s+=$1} END {print s}'
basename /a/b/c.txt .txt; dirname /a/b/c.txt; realpath -m /a/../b
date -d @0 -u +%Y-%m-%d; printf '%s\n' b a | sort | head -1
echo aGVsbG8= | base64 -d; echo; echo hi | sha256sum | cut -c1-16; echo hi | md5sum | cut -c1-8
od -An -c <<< 'ab' | tr -s ' '; diff <(echo 1) <(echo 2) | head -2
du -s x >/dev/null && df / >/dev/null && echo du-df-ok
tail -n2 data; head -c 3 data; echo; nl data | head -1 | tr -s ' \t' ' '
expr 3 + 4; test -d x && echo isdir; env | grep -q '^HOME=' && echo env-ok
yes | head -2; seq -s, 5; factor 12; printf 'x\n' | tee t1 > /dev/null; cat t1
truncate -s 10 t1; stat -c %s t1; split -l 1 data part; ls part* | wc -l
cd /; rm -rf "$d"
