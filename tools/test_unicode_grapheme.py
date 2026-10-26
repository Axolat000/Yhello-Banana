#!/usr/bin/env python3
"""Run host unit tests for Unicode grapheme cluster navigation, deletion, and normalized search."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix='banana-unicode-') as tmp:
    binary = Path(tmp) / 'test_unicode_grapheme'
    cmd = [
        'gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O2', '-g',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-Isdk/include',
        'tools/tests/test_unicode_grapheme.c',
        'third_party/utf8proc/utf8proc.c',
        '-o', str(binary)
    ]
    subprocess.run(cmd, cwd=root, check=True)
    res = subprocess.run([str(binary)], cwd=root, capture_output=True, text=True)
    if res.returncode != 0:
        print("FAIL:\n", res.stderr, file=sys.stderr)
        sys.exit(res.returncode)
    print(res.stdout.strip())
