#!/usr/bin/env python3
"""Run host unit tests for the screenshot service and multi-format clipboard."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix='banana-screenshot-') as tmp:
    binary = Path(tmp) / 'test_screenshot_clipboard'
    cmd = [
        'gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-Ikernel',
        'kernel/kstring.c',
        'tools/tests/screenshot_clipboard.c',
        '-o', str(binary)
    ]
    subprocess.run(cmd, cwd=root, check=True)
    res = subprocess.run([str(binary)], cwd=root, capture_output=True, text=True)
    if res.returncode != 0:
        print("FAIL:\n", res.stderr, file=sys.stderr)
        sys.exit(res.returncode)
    print(res.stdout.strip())
