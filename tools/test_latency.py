#!/usr/bin/env python3
"""Deterministic host regressions for the actual Banana OS implementation."""
import argparse, os, shlex, subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--32', dest='bits32', action='store_true')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
out=root/'build/test/latency'
out.mkdir(parents=True,exist_ok=True)

flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror', '-Ikernel','-Inet', '-I'+str(out),'-include','web/test/shim/types.h']
if a.bits32: flags+=['-m32']
if a.sanitize: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
binary=out/('test.exe' if os.name=='nt' else 'test')
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,*['tools/tests/latency.c'],'-o',str(binary)],cwd=root,check=True)
subprocess.run([str(binary)],cwd=root,check=True)
