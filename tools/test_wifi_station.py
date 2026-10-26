#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Model AP independently creates WPA2 messages and protected 802.11 traffic."""
import argparse, hashlib, hmac, os, random, shlex, subprocess
from pathlib import Path
from cryptography.hazmat.primitives.keywrap import aes_key_wrap
from cryptography.hazmat.primitives.ciphers.aead import AESCCM
ROOT=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sanitize',action='store_true')
ap.add_argument('--32',dest='bits32',action='store_true')
a=ap.parse_args()
out=ROOT/'build/test/wifi_station'
out.mkdir(parents=True,exist_ok=True)
from wifi_test_vectors import write_vectors
write_vectors(out)
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Ikernel','-Inet','-Icrypto','-I'+str(out)]
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
binary=out/'test'
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/wifi_station.c',
                'net/wifi.c','crypto/wifi_crypto.c','crypto/aead.c','crypto/chacha20.c',
                '-o',str(binary)],cwd=ROOT,check=True)
subprocess.run([str(binary)],cwd=ROOT,check=True)
