#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual network selection and USB drivers with host/device models."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sanitize', action='store_true')
ap.add_argument('--32', dest='bits32', action='store_true')
args = ap.parse_args()
out = root / 'build/test/network'
out.mkdir(parents=True, exist_ok=True)
flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-ffunction-sections', '-fdata-sections', '-Ikernel', '-Inet']
if args.bits32:
    flags += ['-m32']
if args.sanitize:
    flags += ['-fsanitize=address,undefined','-fno-sanitize-recover=undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
for name in ['net_selection', 'net_connections', 'usb_ethernet']:
    binary = out / name
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), *flags,
                    f'tools/tests/{name}.c', '-Wl,--gc-sections', '-o', str(binary)],
                   cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)
