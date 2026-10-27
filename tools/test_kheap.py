#!/usr/bin/env python3
"""Compile and run the real kernel heap with host headers on both pointer widths.

Logs and executables: build/test/kheap/. No comparison to an upstream allocator.
Unavailable host toolchains/runtimes are explicitly SKIP, never PASS.
"""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/test/kheap"


def logged(cmd, path, timeout=120):
    with path.open("w") as log:
        log.write("$ " + " ".join(map(str, cmd)) + "\n")
        log.flush()
        try:
            result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            log.write("TIMEOUT\n")
            return None
    return result.returncode


def run(bits):
    directory = OUT / str(bits)
    directory.mkdir(parents=True, exist_ok=True)
    flags = [f"-m{bits}", "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-fno-pie", "-no-pie"]
    # Compile AND execute a minimal libc program before attributing a failure
    # to the test. For example, some containers deny 32-bit syscalls (SIGSYS).
    probe = directory / "probe.c"
    probe.write_text('#include <stdio.h>\nint main(void) { puts("runtime available"); return 0; }\n')
    log = directory / "probe-build.log"
    if logged(["cc", *flags, str(probe), "-o", str(directory / "probe")], log) != 0:
        print(f"SKIP {bits}-bit: host compiler/libc unavailable ({log.relative_to(ROOT)})", flush=True)
        return "SKIP"
    for name in ("kheap.c", "kheap.h"):
        shutil.copyfile(ROOT / "kernel" / name, directory / name)
    for header in (ROOT / "tools/tests/kheap_stub").glob("*.h"):
        shutil.copyfile(header, directory / header.name)
    log = directory / "build.log"
    cmd = ["cc", *flags, "-I", str(directory), str(ROOT / "tools/tests/kheap.c"),
           "-Wl,--defsym=_kernel_end=0x100000", "-o", str(directory / "test")]
    if logged(cmd, log) != 0:
        print(f"FAIL {bits}-bit build ({log.relative_to(ROOT)})", flush=True)
        return "FAIL"
    log = directory / "probe-run.log"
    rc = logged([str(directory / "probe")], log)
    if rc != 0:
        print(f"SKIP {bits}-bit: host runtime probe returned {rc} ({log.relative_to(ROOT)})", flush=True)
        return "SKIP"
    log = directory / "run.log"
    rc = logged([str(directory / "test")], log)
    print(log.read_text(), end="", flush=True)
    status = "PASS" if rc == 0 else "FAIL"
    print(f"{status} {bits}-bit: exit {rc} ({log.relative_to(ROOT)})", flush=True)
    return status


def main():
    results = [run(bits) for bits in (64, 32)]
    print(f"Heap host results: {results.count('PASS')} passed, {results.count('SKIP')} skipped, {results.count('FAIL')} failed")
    return 0 if "PASS" in results and "FAIL" not in results else 1


if __name__ == "__main__":
    sys.exit(main())
