#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Format an ESP with its file data aligned to ISO9660 blocks."""
import os
import struct
import subprocess
import sys


def data_start(boot):
    bps = struct.unpack_from("<H", boot, 11)[0]
    reserved = struct.unpack_from("<H", boot, 14)[0]
    roots = struct.unpack_from("<H", boot, 17)[0]
    fat = struct.unpack_from("<H", boot, 22)[0] or struct.unpack_from("<I", boot, 36)[0]
    if bps != 512 or not boot[13] or not fat:
        raise ValueError("unsupported FAT geometry")
    return reserved + boot[16] * fat + (roots * 32 + bps - 1) // bps


def main():
    path = sys.argv[1]
    for reserved in range(1, 9):
        args = ["mformat", "-i", path, "-v", "BANANA_EFI", "-c", "8", "-R", str(reserved)]
        if os.path.getsize(path) >= 250 * 1024 * 1024:
            args.append("-F")
        subprocess.run(args + ["::"], check=True)
        with open(path, "rb") as f:
            if data_start(f.read(512)) % 4 == 0:
                return
    raise SystemExit("cannot align EFI file data to ISO blocks")


if __name__ == "__main__":
    main()
