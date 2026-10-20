#!/usr/bin/env python3
"""Characterize real DHCP timers and the Network Settings rendering on the host."""
import argparse
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--32', dest='bits32', action='store_true')
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
out = root / 'build/test/dhcp'
out.mkdir(parents=True, exist_ok=True)
# Extract the actual renderer, not a copy of its state/label logic. Drawing,
# driver and time shims in the harness do not validate framebuffer layout.
source = (root / 'kernel/settings.c').read_text()
start = source.index('static void draw_network(void) {')
end = source.index('\n}\n', start) + 3
(out / 'draw_network.inc').write_text(source[start:end])
flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-Ikernel', '-Inet', '-I' + str(out),
         '-include', 'web/test/shim/types.h']
if a.bits32:
    flags += ['-m32']
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
binary = out / ('test32' if a.bits32 else 'test64')
subprocess.run(['cc', *flags, 'tools/tests/dhcp_state.c', 'net/dhcp.c', '-o', binary],
               cwd=root, check=True)
subprocess.run([binary], cwd=root, check=True)
