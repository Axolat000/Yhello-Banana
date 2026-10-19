#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check firmware RAM accounting, including memory above 4 GiB, without QEMU."""
from pathlib import Path
import argparse
import subprocess

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--32',dest='bits32',action='store_true')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
out=root/'build/test/bootmem';out.mkdir(parents=True,exist_ok=True)
flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Iweb/test/shim','-Ikernel',
       '-include','web/test/shim/types.h']
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
binary=out/('test32' if a.bits32 else 'test64')
subprocess.run(['cc',*flags,'tools/tests/bootmem.c','kernel/bootmem.c',
                'kernel/bootmem_test.c','-o',binary],cwd=root,check=True)
subprocess.run([binary],cwd=root,check=True)
