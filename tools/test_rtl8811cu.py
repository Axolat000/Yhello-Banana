#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual RTL8811CU radio/DMA frontend and WPA2 station against hardware models."""
import argparse,os,shlex,subprocess
from pathlib import Path
from wifi_test_vectors import write_vectors
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sanitize',action='store_true')
p.add_argument('--32',dest='bits32',action='store_true')
a=p.parse_args()
out=root/'build/test/rtl8811cu';out.mkdir(parents=True,exist_ok=True);write_vectors(out)
firmware=(root/'firmware/realtek/rtw8821c_fw.bin').read_bytes()
(out/'cu_test_firmware.h').write_text('static const unsigned char fw_bytes[] __attribute__((used))={'+','.join(map(str,firmware))+'};\n')
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-const-variable',
       '-Ikernel','-Inet','-Icrypto','-Iusb','-I'+str(out)]
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-pie','-no-pie','-fno-omit-frame-pointer']
binary=out/'test'
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/rtl8811cu.c',
                'net/wifi.c','crypto/wifi_crypto.c','crypto/aead.c','crypto/chacha20.c',
                '-o',str(binary)],cwd=root,check=True)
subprocess.run([str(binary)],cwd=root,check=True)

hal=out/'hal'
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/rtl8811cu_hal.c',
                '-o',str(hal)],cwd=root,check=True)
subprocess.run([str(hal)],cwd=root,check=True)
