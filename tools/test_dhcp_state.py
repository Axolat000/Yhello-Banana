#!/usr/bin/env python3
"""Compile and run deterministic host tests of the actual network implementation."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--32', dest='bits32', action='store_true')
a = p.parse_args()
out = root / 'build/test/dhcp_state'
out.mkdir(parents=True, exist_ok=True)
flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-Ikernel', '-Inet', '-include', 'web/test/shim/types.h']
if a.bits32:
    flags += ['-m32']
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
binary = out / ('test.exe' if os.name == 'nt' else 'test')
subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), *flags,
                'tools/tests/dhcp_state.c', 'net/dhcp.c', '-o', str(binary)], cwd=root, check=True)
subprocess.run([str(binary)], cwd=root, check=True)
