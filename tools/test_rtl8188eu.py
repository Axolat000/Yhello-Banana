#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""RTL8188EU register/USB/calibration model with native WPA2 station integration."""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
from wifi_test_vectors import write_vectors
root=Path(__file__).resolve().parents[1]
firmware=(root/'firmware/realtek/rtl8188eufw.bin').read_bytes()
if len(firmware)!=13904 or hashlib.sha256(firmware).hexdigest()!='1241ddbfc87f0495e0bf09d8b94c94680b60a3d8eeab89462b3c4d8d3e8a1ee0':
    raise SystemExit('RTL8188EU firmware checksum/length mismatch')
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sanitize',action='store_true')
ap.add_argument('--32',dest='bits32',action='store_true')
a=ap.parse_args()
out=root/'build/test/rtl8188eu'
out.mkdir(parents=True,exist_ok=True)
write_vectors(out)
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Ikernel','-Inet','-Icrypto','-I'+str(out)]
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
binary=out/'test'
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/rtl8188eu.c',
                'net/wifi.c','crypto/wifi_crypto.c','crypto/aead.c','crypto/chacha20.c',
                '-o',str(binary)],cwd=root,check=True)
subprocess.run([str(binary)],cwd=root,check=True)
