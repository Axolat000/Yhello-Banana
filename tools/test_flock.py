#!/usr/bin/env python3
"""Run flock contract and concurrency tests on host (no VM or disk required)."""
from pathlib import Path
import argparse
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
out = root / 'build/test/flock'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'test64'
flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
         '-ffunction-sections', '-fdata-sections', '-Iweb/test/shim', '-Ikernel',
         '-include', 'web/test/shim/types.h']
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
              '-fno-pie', '-no-pie']
subprocess.run(['cc', *flags, 'tools/tests/test_flock.c', '-Wl,--gc-sections',
                '-o', str(binary)], cwd=root, check=True)
subprocess.run([str(binary)], cwd=root, check=True)
