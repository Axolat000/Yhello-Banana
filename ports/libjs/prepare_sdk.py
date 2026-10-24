#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare private third-party declaration headers, never host libc headers."""
from pathlib import Path
import shutil,subprocess
here=Path(__file__).resolve().parent
out=here/'build/banana/include';out.mkdir(parents=True,exist_ok=True)
if subprocess.check_output(['g++','-dumpversion'],text=True).strip().split('.')[0]!='14':
    raise SystemExit('SDK prototype currently requires GCC 14 (Debian 13)')
if subprocess.check_output(['pkg-config','--modversion','icu-uc'],text=True).strip()!='76.1':
    raise SystemExit('SDK declarations must match the pinned ICU 76.1 data/runtime')
triple=subprocess.check_output(['gcc','-dumpmachine'],text=True).strip()
for name in ['unicode','openssl','fast_float']:
    shutil.copytree(Path('/usr/include')/name,out/name,dirs_exist_ok=True)
shutil.copytree(Path('/usr/include')/triple/'openssl',out/'openssl',dirs_exist_ok=True)
for source,name in [
    (here/'src/simdjson-4.2.4/singleheader/simdjson.h','simdjson.h'),
    (Path('/usr/include/tommath.h'),'tommath.h'),
    (here/'include/shadow.h','shadow.h')]:
    shutil.copy2(source,out/name)
print('SDK declarations prepared; C runtime headers come only from the Banana SDK')
