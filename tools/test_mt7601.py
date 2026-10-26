#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""MT7601U USB/register/firmware model running the actual radio and station code."""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
from wifi_test_vectors import write_vectors
root=Path(__file__).resolve().parents[1]
firmware=(root/'firmware/mediatek/mt7601u.bin').read_bytes()
if len(firmware)!=45412 or hashlib.sha256(firmware).hexdigest()!='4511b1d840e22aea2bf5fdca419c91c0d94cbfb291b9ac4f8be6d9100d1a7046':
    raise SystemExit('MT7601U firmware checksum/length mismatch')
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sanitize',action='store_true')
ap.add_argument('--32',dest='bits32',action='store_true')
a=ap.parse_args()
out=root/'build/test/mt7601'
out.mkdir(parents=True,exist_ok=True)
write_vectors(out)
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Ikernel','-Inet','-Icrypto','-I'+str(out)]
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
binary=out/'test'
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/mt7601.c',
                'net/wifi.c','crypto/wifi_crypto.c','crypto/aead.c','crypto/chacha20.c',
                '-o',str(binary)],cwd=root,check=True)
subprocess.run([str(binary)],cwd=root,check=True)
