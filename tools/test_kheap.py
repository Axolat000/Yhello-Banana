#!/usr/bin/env python3
"""Tests kernel/kheap.c (the kernel's heap) on Linux, without Banana OS:
built with stand-in headers (tools/tests/kheap_stub), its heap in memory
mapped below 4 GiB. tools/tests/kheap.c runs it hard and times it against
the kheap.c of BASE from git (default: build 193's, before the bins) on the
same workload.

usage: tools/test_kheap.py [BASE]"""
import subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / "build/test/kheap"
out.mkdir(parents=True, exist_ok=True)
base = sys.argv[1] if len(sys.argv) > 1 else "d9c9071"     # (build 193: the heap before the bins)
flags = ["-O2", "-g", "-Wall", "-Wextra"]
syms = ["kheap_init", "kmalloc", "kzalloc", "kfree", "krealloc", "kmalloc_aligned", "kheap_total_bytes",
        "kheap_used_bytes", "kheap_largest_free", "kheap_set_reclaim", "kheap_add_region"]

import shutil

def build(name, kheap_c, kheap_h):
    """kheap.c in a folder of its own with the stand-in headers (a quoted
    include looks next to the file first: the kernel's types.h would win)"""
    d = out / name
    d.mkdir(exist_ok=True)
    (d / "kheap.c").write_text(kheap_c)
    (d / "kheap.h").write_text(kheap_h)
    for h in (root / "tools/tests/kheap_stub").iterdir():
        shutil.copy(h, d / h.name)
    subprocess.run(["gcc", *flags, "-I", str(d), "-c", str(d / "kheap.c"), "-o", str(d / "kheap.o")], check=True)
    return d / "kheap.o"

def git_show(path):
    return subprocess.run(["git", "-C", str(root), "show", f"{base}:{path}"], capture_output=True, text=True, check=True).stdout

new_o = build("new", (root / "kernel/kheap.c").read_text(), (root / "kernel/kheap.h").read_text())
old_o = build("old", git_show("kernel/kheap.c"), git_show("kernel/kheap.h"))
old_renamed = out / "kheap_old_renamed.o"
subprocess.run(["objcopy", *[f"--redefine-sym={s}=old_{s}" for s in syms], str(old_o), str(old_renamed)], check=True)
exe = out / "test"
subprocess.run(["gcc", "-O1", "-g", str(root / "tools/tests/kheap.c"), str(new_o), str(old_renamed),
                "-Wl,--defsym=_kernel_end=0x100000", "-o", str(exe)], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
