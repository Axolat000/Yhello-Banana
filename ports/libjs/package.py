#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble the optional SDK archive, retain licenses and build the test BPK."""
from pathlib import Path
import json,os,re,subprocess
here=Path(__file__).resolve().parent;root=here.parent.parent
out=here/'build/banana'
manifest=json.loads((here/'sources.json').read_text())
source=Path(os.environ.get('LADYBIRD_SOURCE',str(here/'src'/manifest['ladybird']['directory'])))
base=['gcc','-O2','-fPIC','-ffunction-sections','-fdata-sections','-mno-red-zone',
      '-fno-stack-protector','-nostdinc','-I'+str(root/'sdk/include'),
      '-I'+str(root/'ports/llvm/include'),'-isystem',
      subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()]
for src,name in [(root/'ports/llvm/compat.c','locale-compat'),(here/'compat.c','compat')]:
    subprocess.run([*base,'-c',str(src),'-o',str(out/(name+'.o'))],check=True)
# Keep GNU locale ABI support private and weak; actual SDK functions win.
subprocess.run(['objcopy','--weaken',str(out/'locale-compat.o')],check=True)
objects=sorted((out/'obj').glob('*.o'))+sorted((out/'deps').glob('*.o'))
objects += [out/'locale-compat.o',out/'compat.o']
if len(objects)<100:raise SystemExit('missing compiled SDK objects')
archive=out/'libjs-prototype.a';archive.unlink(missing_ok=True)
subprocess.run(['ar','rcs',str(archive),*[str(p) for p in objects]],check=True)
parts=['Banana OS LibJS prototype - source revisions\n'+(here/'sources.json').read_text()]
def notice(path):
    parts.append('\n===== '+str(path.relative_to(root) if path.is_relative_to(root) else path)+' =====\n'+path.read_text())
notice(root/'LICENSE');notice(source/'LICENSE')
# Retain individual notices, including compiled-but-unlinked libraries.
seen=set()
for base_dir in [source/'AK',source/'Libraries']:
    for path in sorted(base_dir.rglob('*')):
        if path.suffix not in ['.h','.cpp','.c','.inc'] or not path.is_file():continue
        text=path.read_text(errors='replace');m=re.match(r'/\*.*?\*/',text,re.S)
        if m and ('Copyright' in m[0] or 'SPDX-License' in m[0]) and m[0] not in seen:
            seen.add(m[0]);parts.append(m[0])
for path in [
    here/'src/icu/LICENSE',here/'src/simdutf-7.5.0/LICENSE-MIT',
    here/'src/simdjson-4.2.4/LICENSE',here/'src/libtommath-1.3.0/LICENSE',
    Path('/usr/share/doc/libfast-float-dev/copyright'),
    Path('/usr/share/doc/libstdc++-14-dev/copyright')]:
    notice(path)
(out/'THIRD_PARTY_NOTICES.txt').write_text('\n'.join(parts))
subprocess.run(['make','-C',str(root/'apps/libjsprobe'),'-j'+os.environ.get('JOBS','2')],check=True)
elf=root/'apps/libjsprobe/build/app-x86_64'
dynamic=subprocess.check_output(['readelf','-d',str(elf)],text=True)
if '(NEEDED)' in dynamic:raise SystemExit('host shared library dependency in SDK image')
program=subprocess.check_output(['readelf','-l',str(elf)],text=True)
if 'INTERP' in program:raise SystemExit('host interpreter in SDK image')
subprocess.run(['python3',str(root/'sdk/tools/bpkg'), 'check',
                str(root/'apps/libjsprobe/libjsprobe-x86_64.bpk')],check=True)
for path in [elf,root/'apps/libjsprobe/libjsprobe-x86_64.bpk',root/'apps/libjsprobe/libjsprobe-x86_64.bpk.z']:
    print(path.name+': '+str(path.stat().st_size)+' bytes')
