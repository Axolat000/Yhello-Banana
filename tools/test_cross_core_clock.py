#!/usr/bin/env python3
"""Characterization and qualification tests for cross-core TSC clock synchronization."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/test/clock_cross_core'
out.mkdir(parents=True, exist_ok=True)

test_src = root / 'tools/tests/test_cross_core_clock.c'
binary = out / 'test_cross_core'

flags = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Ikernel', '-Inet']
subprocess.run(['cc', *flags, str(test_src), '-o', str(binary)], cwd=root, check=True)
subprocess.run([str(binary)], cwd=root, check=True)
print("Cross-core clock unit qualification passed.")
