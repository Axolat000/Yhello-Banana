#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native RTL8125/A/B/D MAC/PHY/firmware and DMA model; no physical validation."""
import argparse,hashlib,os,shlex,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
if hashlib.sha256((root/'third_party/rge/rgemcode.h').read_bytes()).hexdigest()!='8ef508a048f687f6924feddcb839d6af7638c8a8840aa20103a71e2a48d7c1a5':
    raise SystemExit('RTL8125 firmware data differs from the pinned upstream snapshot')
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sanitize',action='store_true')
ap.add_argument('--32',dest='bits32',action='store_true')
a=ap.parse_args()
out=root/'build/test/r8125';out.mkdir(parents=True,exist_ok=True)
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Ikernel','-Inet']
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/r8125.c','third_party/rge/rge_hw.c',
                '-o',str(out/'test')],cwd=root,check=True)
subprocess.run([str(out/'test')],cwd=root,check=True)
