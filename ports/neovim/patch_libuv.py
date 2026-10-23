#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Use a bounded byte search in libuv's ARM model-name parser.

The parser is compiled on x86 too; the Banana SDK does not provide GNU
memmem, and GCC 14 rejects its otherwise unused implicit declaration.
"""
from pathlib import Path
import sys

p = Path(sys.argv[1]) / 'src/unix/linux.c'
s = p.read_text()
old = '      p = memmem(parts, sizeof(parts) - 1, p, n + 1);'
new = '''      /* Banana OS: bounded search without GNU memmem. */
      const char* match = NULL;
      size_t needle_len = (size_t)n + 1;
      if (needle_len <= sizeof(parts) - 1) {
        for (size_t j = 0; j <= sizeof(parts) - 1 - needle_len; j++) {
          if (!memcmp(parts + j, p, needle_len)) { match = parts + j; break; }
        }
      }
      p = match;'''
if old in s:
    p.write_text(s.replace(old, new, 1))
elif 'Banana OS: bounded search without GNU memmem.' not in s:
    raise SystemExit('libuv model parser changed; inspect its memmem use')
