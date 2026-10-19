#!/usr/bin/env python3
"""Run the kernel fd_sync contract tests on Linux (no VM or disk required)."""
from pathlib import Path
import argparse
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--32', dest='bits32', action='store_true')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
out = root / 'build/test/fd-sync'
out.mkdir(parents=True, exist_ok=True)
binary = out / ('test32' if args.bits32 else 'test64')
flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
         '-ffunction-sections', '-fdata-sections', '-Iweb/test/shim', '-Ikernel',
         '-include', 'web/test/shim/types.h']
if args.bits32:
    flags += ['-m32']
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
              '-fno-pie', '-no-pie']
subprocess.run(['cc', *flags, 'tools/tests/fd_sync.c', '-Wl,--gc-sections',
                '-o', str(binary)], cwd=root, check=True)
subprocess.run([str(binary)], cwd=root, check=True)
