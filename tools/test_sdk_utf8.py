#!/usr/bin/env python3
"""Run host unit and stress tests for the SDK restartable UTF-8 module."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix='banana-utf8-') as tmp:
    binary = Path(tmp) / 'test_sdk_utf8'
    cmd = [
        'gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O2',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I', str(root / 'sdk/include'),
        str(root / 'sdk/lib/utf8.c'),
        str(root / 'tools/tests/test_sdk_utf8.c'),
        '-o', str(binary)
    ]
    subprocess.run(cmd, cwd=root, check=True)
    res = subprocess.run([str(binary)], cwd=root, capture_output=True)
    if res.returncode != 0:
        print("FAIL:\n", res.stderr.decode('utf-8', errors='replace'), file=sys.stderr)
        sys.exit(res.returncode)
    print(res.stdout.decode('utf-8', errors='replace').strip())
