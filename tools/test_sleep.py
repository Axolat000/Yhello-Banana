#!/usr/bin/env python3
"""Check the ACPI sleep package parser without suspending any machine."""
from pathlib import Path
import argparse
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--32',dest='bits32',action='store_true')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/'build/test/sleep';out.mkdir(parents=True,exist_ok=True)
flags=['-O1','-g','-Wall','-Wextra','-Ikernel','-include','web/test/shim/types.h']
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-pie','-no-pie']
binary=out/('test32' if a.bits32 else 'test64')
subprocess.run(['cc',*flags,'tools/tests/sleep.c','kernel/acpi_sleep_aml.c','kernel/acpi_sleep_test.c','kernel/bootmem.c','-o',binary],cwd=root,check=True)
subprocess.run([binary],check=True)
