#!/usr/bin/env python3
"""Run host characterization tests for DNS resolution and SDK socket address mapping."""
import argparse
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--32', dest='bits32', action='store_true')
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
out = root / 'build/test/dns_socket'
out.mkdir(parents=True, exist_ok=True)

# Extract socket resolver and lookup functions from sdk/lib/socket.c
source = (root / 'sdk/lib/socket.c').read_text()
start = source.index('/* ── names ────────────────────────────────────────────────────────── */')
end = source.index('\nint gethostname(char* name, size_t len) {')
(out / 'socket_impl.inc').write_text(source[start:end])

flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
         '-Ikernel', '-Inet', '-Isdk/include', '-I' + str(out),
         '-include', 'web/test/shim/types.h']
if a.bits32:
    flags += ['-m32']
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']

binary = out / ('test32' if a.bits32 else 'test64')
subprocess.run(['cc', *flags, 'tools/tests/dns_socket_state.c', '-o', binary],
               cwd=root, check=True)
subprocess.run([binary], cwd=root, check=True)
