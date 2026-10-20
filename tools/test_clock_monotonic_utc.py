#!/usr/bin/env python3
"""Run host characterization tests for monotonic clock, UTC separation, and TCP delay invariance."""
import argparse
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--32', dest='bits32', action='store_true')
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
out = root / 'build/test/clock_monotonic'
out.mkdir(parents=True, exist_ok=True)

flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
         '-Ikernel', '-Inet', '-Isdk/include']
if a.bits32:
    flags += ['-m32']
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']

binary = out / ('test32' if a.bits32 else 'test64')
subprocess.run(['cc', *flags, 'tools/tests/clock_monotonic_utc.c', '-o', binary],
               cwd=root, check=True)
subprocess.run([binary], cwd=root, check=True)

# SNTP client host test
ntp_binary = out / ('test_ntp32' if a.bits32 else 'test_ntp64')
ntp_flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Inet']
if a.bits32:
    ntp_flags += ['-m32']
if a.sanitize:
    ntp_flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
subprocess.run(['cc', *ntp_flags, 'tools/tests/test_ntp_client.c', 'net/ntp.c', '-o', ntp_binary],
               cwd=root, check=True)
subprocess.run([ntp_binary], cwd=root, check=True)
