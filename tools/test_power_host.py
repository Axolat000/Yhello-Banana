#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native uACPI kernel callbacks and ACPI EC port protocol tests."""
import argparse,os,shlex,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sanitize',action='store_true')
ap.add_argument('--32',dest='bits32',action='store_true')
a=ap.parse_args()
out=root/'build/test/power-host';out.mkdir(parents=True,exist_ok=True)
flags=['-O1','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
       '-Ikernel','-Inet','-Iusb','-Ithird_party/uacpi/include','-Ithird_party/uacpi/platform',
       '-DUACPI_OVERRIDE_TYPES','-DUACPI_USE_BUILTIN_STRING']
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,'tools/tests/power_host.c','-o',str(out/'test')],cwd=root,check=True)
subprocess.run([str(out/'test')],cwd=root,check=True)
