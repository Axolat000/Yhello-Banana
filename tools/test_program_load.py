#!/usr/bin/env python3
"""Exercise the actual executable preparation path with cache eviction and I/O faults."""
import argparse, os, shlex, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sanitize',action='store_true')
a=ap.parse_args()
out=root/'build/test/program-load';out.mkdir(parents=True,exist_ok=True)
s=(root/'kernel/app.c').read_text()
def function(sig):
    start=s.index(sig);opening=s.index('{',start);end=opening+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]+'\n'
(out/'program_pin.inc').write_text(function('static fs_file_t* pin_program('))
(out/'program_prepare.inc').write_text(function('static app_proc_t* prepare('))
flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(out)]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie']
exe=out/'test'
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/program_load.c','-o',str(exe)],cwd=root,check=True)
subprocess.run([str(exe)],cwd=root,check=True)
