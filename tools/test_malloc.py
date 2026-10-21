#!/usr/bin/env python3
"""Tests sdk/lib/malloc.c (the apps' heap) on Linux, without Banana OS:
built with the SDK's own flags, its malloc/free/... renamed b_* so they do
not clash with Linux's, then tools/tests/malloc.c runs it hard (random
blocks with every byte checked, realloc, alignment, 8 threads freeing each
other's blocks) under a stand-in for the system's call table.

usage: tools/test_malloc.py     (64-bit: the heap is the 64-bit SDK's)"""
import subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / "build/test/malloc"
out.mkdir(parents=True, exist_ok=True)
sdk_flags = ["-m64", "-mno-red-zone", "-ffreestanding", "-fno-stack-protector", "-fpie", "-nostdlib", "-nostdinc",
             "-fno-builtin", "-fno-builtin-malloc", "-fno-tree-loop-distribute-patterns", "-O2", "-Wall", "-Wextra",
             "-I", str(root / "sdk/include")]
sdk_flags += ["-ffunction-sections", "-fdata-sections"]
obj = out / "malloc.o"
subprocess.run(["gcc", *sdk_flags, "-c", str(root / "sdk/lib/malloc.c"), "-o", str(obj)], check=True)
renamed = out / "malloc-renamed.o"
syms = ["malloc", "free", "realloc", "calloc", "malloc_usable_size", "posix_memalign", "aligned_alloc", "memalign"]
subprocess.run(["objcopy", *[f"--redefine-sym={s}=b_{s}" for s in syms], str(obj), str(renamed)], check=True)
posix_obj = out / "posix.o"
subprocess.run(["gcc", *sdk_flags, "-c", str(root / "sdk/lib/posix.c"), "-o", str(posix_obj)], check=True)
subprocess.run(["objcopy", *[f"--redefine-sym={s}=b_{s}" for s in syms], str(posix_obj)], check=True)
exe = out / "test"
subprocess.run(["gcc", "-O1", "-g", "-pthread", "-no-pie", "-Wl,--gc-sections", "-Wl,--unresolved-symbols=ignore-in-object-files", "-idirafter", str(root / "sdk/include"), "-include", "stddef.h",
                str(root / "tools/tests/malloc.c"), str(renamed), str(posix_obj), "-o", str(exe)], check=True)
for mode in ["heap", "old-api", "short-api", "no-map", "no-unmap"]:
    subprocess.run([str(exe), mode], check=True)
