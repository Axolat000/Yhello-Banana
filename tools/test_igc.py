#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Intel I225/I226 native HAL + DMA register model; no physical PHY validation."""
import argparse, os, shlex, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sanitize',action='store_true')
ap.add_argument('--32',dest='bits32',action='store_true')
a=ap.parse_args()
out=root/'build/test/igc'
out.mkdir(parents=True,exist_ok=True)
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Ikernel','-Inet']
if a.bits32: flags+=['-m32']
if a.sanitize: flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/igc.c',
                *[str(p.relative_to(root)) for p in sorted((root/'third_party/igc').glob('*.c'))],
                '-o',str(out/'test')],cwd=root,check=True)
subprocess.run([str(out/'test')],cwd=root,check=True)
