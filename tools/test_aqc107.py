#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual AQC107 firmware/PHY/DMA code against register and packet models."""
import argparse,os,shlex,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sanitize',action='store_true')
p.add_argument('--32',dest='bits32',action='store_true')
a=p.parse_args()
out=root/'build/test/aqc107';out.mkdir(parents=True,exist_ok=True)
flags=['-O1','-g','-Wall','-Wextra','-Werror','-DAQC107_MODEL','-Ikernel','-Inet']
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-pie','-no-pie','-fno-omit-frame-pointer']
binary=out/'test'
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/aqc107.c','-o',str(binary)],cwd=root,check=True)
subprocess.run([str(binary)],cwd=root,check=True)
