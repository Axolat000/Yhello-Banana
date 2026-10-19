#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the monotonic clock arithmetic without privileged hardware access."""
from pathlib import Path
import argparse
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--32',dest='bits32',action='store_true')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/'build/test/clock';out.mkdir(parents=True,exist_ok=True)
flags=['-O1','-g','-Wall','-Wextra','-Ikernel','-include','web/test/shim/types.h']
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-pie','-no-pie']
binary=out/('test32' if a.bits32 else 'test64')
subprocess.run(['cc',*flags,'tools/tests/clock.c','kernel/timer_math_test.c','-o',binary],cwd=root,check=True)
subprocess.run([binary],check=True)
