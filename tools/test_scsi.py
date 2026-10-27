#!/usr/bin/env python3
import subprocess
import sys
import os

def main():
    print("Running virtio-scsi driver verification tests...")
    test_src = "tools/tests/test_virtio_scsi.c"
    test_bin = "build/test_virtio_scsi"
    os.makedirs("build", exist_ok=True)
    compile_cmd = ["gcc", "-Wall", "-Wextra", "-O2", test_src, "-o", test_bin]
    res = subprocess.run(compile_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("Compilation failed:\n", res.stderr)
        return 1
    run_res = subprocess.run([test_bin], capture_output=True, text=True)
    print(run_res.stdout)
    if run_res.returncode != 0:
        print("Test execution failed:\n", run_res.stderr)
        return run_res.returncode
    return 0

if __name__ == "__main__":
    sys.exit(main())
