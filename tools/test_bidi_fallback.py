#!/usr/bin/env python3
"""Run the Font Fallback and Bidi Algorithm test suite with FreeType & HarfBuzz.

Verifies:
1. French text, accents and ligatures.
2. Arabic RTL complex script text shaped with HarfBuzz.
3. Mixed bidirectional text itemization and reordering.
4. Font fallback chain resolution across fonts/.
5. AddressSanitizer and UndefinedBehaviorSanitizer safety.
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]

def run_tests():
    cmd = ["bash", "ports/harfbuzz/test/run.sh"]
    print("Running HarfBuzz & Bidi fallback tests...")
    res = subprocess.run(cmd, cwd=root)
    if res.returncode != 0:
        print("HarfBuzz & Bidi fallback tests FAILED")
        sys.exit(res.returncode)
    print("HarfBuzz & Bidi fallback tests PASSED")

if __name__ == "__main__":
    run_tests()
