#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual AR9280 radio/DMA frontend and WPA2 station against hardware models."""
import argparse,os,shlex,subprocess
from pathlib import Path
from wifi_test_vectors import write_vectors
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sanitize',action='store_true')
p.add_argument('--32',dest='bits32',action='store_true')
a=p.parse_args()
out=root/'build/test/ar9280';out.mkdir(parents=True,exist_ok=True);write_vectors(out)
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-const-variable',
       '-Ikernel','-Inet','-Icrypto','-I'+str(out)]
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-pie','-no-pie','-fno-omit-frame-pointer']
binary=out/'test'
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/ar9280.c',
                'net/wifi.c','crypto/wifi_crypto.c','crypto/aead.c','crypto/chacha20.c',
                '-o',str(binary)],cwd=root,check=True)
subprocess.run([str(binary)],cwd=root,check=True)
