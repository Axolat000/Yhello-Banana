#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests net/tcp.c on Linux, without Banana OS: both ends of a connection
in one process over a simulated link (delay, bandwidth, a bottleneck queue,
random loss) on a virtual clock - tools/tests/tcp_sim.c. Every download is
checked byte for byte; the same downloads with the tcp.c of BASE from git
(default: build 195's, before window scaling and SACK) show the difference.

usage: tools/test_tcp.py [BASE]"""
import shutil, subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / "build/test/tcp"
out.mkdir(parents=True, exist_ok=True)
base = sys.argv[1] if len(sys.argv) > 1 else "7381529"    # (build 195)

def build(name, tcp_c, tcp_h, new):
    """tcp.c in a folder of its own with the stand-in headers (a quoted
    include looks next to the file first)"""
    d = out / name
    d.mkdir(exist_ok=True)
    (d / "tcp.c").write_text(tcp_c)
    (d / "tcp.h").write_text(tcp_h)
    for h in (root / "tools/tests/tcp_stub").iterdir():
        shutil.copy(h, d / h.name)
    exe = d / "sim"
    subprocess.run(["gcc", "-O2", "-g", "-Wall", "-Wno-unused-function", "-I", str(d), *(["-DNEW_TCP"] if new else []),
                    str(d / "tcp.c"), str(root / "tools/tests/tcp_sim.c"), "-o", str(exe)], check=True)
    return exe

def git_show(path):
    return subprocess.run(["git", "-C", str(root), "show", f"{base}:{path}"], capture_output=True, text=True, check=True).stdout

def run(exe):
    p = subprocess.run([str(exe)], capture_output=True, text=True)
    rows, other = [], []
    for line in p.stdout.splitlines():
        f = line.split()
        if f and f[0] == "RESULT":
            rows.append((int(f[1]), int(f[2]), int(f[3]), int(f[4]), float(f[5]), int(f[6]), int(f[7]), int(f[8]), int(f[9])))
        else:
            other.append(line)
    return p.returncode, rows, other

new_rc, new, new_other = run(build("new", (root / "net/tcp.c").read_text(), (root / "net/tcp.h").read_text(), True))
old_rc, old, old_other = run(build("old", git_show("net/tcp.c"), git_show("net/tcp.h"), False))

fail = 0
for line in new_other:
    print(line)
    if line.startswith("FAIL"): fail += 1
for line in old_other:
    if line.startswith("FAIL"): print("(before) " + line)
print(f"{'RTT':>6} {'link':>9} {'loss':>6} {'MiB':>4}   {'now':>10} {'before':>10}  window      options")
for n, o in zip(new, old):
    rtt, mbit, loss, mib, mbps, ok, win, ws, sack = n
    print(f"{rtt:4} ms {mbit:4} Mb/s {loss / 100:5.2f}% {mib:4}   {mbps:6.2f} MB/s {o[4]:6.2f} MB/s  {win // 1024:5} KiB  "
          f"{'wscale' if ws else '-'} {'sack' if sack else '-'}{'' if ok else '   FAIL (data)'}")
    if not ok: fail += 1
    if not ws or not sack: print("FAIL  window scaling / SACK not agreed"); fail += 1
# at 100 ms round trip with no loss the old window (64 KiB, 32 KiB sent at a time) is the limit
far = [(n, o) for n, o in zip(new, old) if n[0] == 100 and n[2] == 0]
for n, o in far:
    if n[4] < 4 * o[4]:
        print(f"FAIL  100 ms round trip: {n[4]:.2f} MB/s is not 4x {o[4]:.2f}"); fail += 1
print(f"tcp: {'FAILED' if fail else 'all checks passed'} ({fail} failure{'' if fail == 1 else 's'})")
sys.exit(1 if fail or new_rc else 0)
