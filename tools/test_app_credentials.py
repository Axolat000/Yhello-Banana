#!/usr/bin/env python3
"""Host regression tests for kernel app credential selection and task lookup."""
import argparse
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--32', dest='bits32', action='store_true')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
out = root / 'build/test/app_credentials'
out.mkdir(parents=True, exist_ok=True)
flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-ffunction-sections',
         '-fdata-sections', '-Iweb/test/shim', '-Ikernel',
         '-include', 'web/test/shim/types.h', '-Wl,--gc-sections']
if args.bits32:
    flags += ['-m32']
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
              '-fno-pie', '-no-pie']
binary = out / ('test32' if args.bits32 else 'test64')
subprocess.run(['cc', *flags, 'tools/tests/app_credentials.c', '-o', str(binary)],
               cwd=root, check=True)
subprocess.run([str(binary)], cwd=root, check=True)
