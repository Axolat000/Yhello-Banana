d=$(mktemp -d); cd "$d"; touch a.txt b.txt c.log .hidden 'sp ace.txt'; mkdir sub; touch sub/x.txt
echo *.txt; echo ?.txt; echo [ab].*; echo *.{txt,log}
echo {1..5} {a..e} {01..03} {1..10..3} x{A,B}y
shopt -s nullglob; echo none: *.zzz; shopt -u nullglob; echo kept: *.zzz
shopt -s dotglob; echo *hid*; shopt -u dotglob
shopt -s globstar; echo **/*.txt; shopt -u globstar
shopt -s extglob
echo !(*.txt); echo @(a|b).txt
shopt -u extglob
for f in *.txt; do echo "[$f]"; done
echo ~ | grep -q / && echo tilde-ok
ls | wc -l
cd /; rm -rf "$d"
