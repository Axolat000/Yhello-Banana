#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test the actual window/event implementation with host framebuffer/heap shims."""
from pathlib import Path
import argparse
import subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--32',dest='bits32',action='store_true')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
out=root/'build/test/mouse';out.mkdir(parents=True,exist_ok=True)
flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-ffunction-sections','-fdata-sections',
       '-Iweb/test/shim','-Ikernel','-include','web/test/shim/types.h']
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
binary=out/('test32' if a.bits32 else 'test64')
sdk_object=out/('sdk32.o' if a.bits32 else 'sdk64.o')
subprocess.run(['cc',*flags,'-Isdk/include','-c','sdk/lib/banana.c','-o',sdk_object],cwd=root,check=True)
subprocess.run(['cc',*flags,'tools/tests/mouse_capture.c','kernel/mouse_capture.c',
                'kernel/mouse_capture_test.c','kernel/appwin.c',sdk_object,'-Wl,--gc-sections','-o',binary],cwd=root,check=True)
subprocess.run([binary],cwd=root,check=True)
