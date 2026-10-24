#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Use real GCC RTTI/throw support in this optional program, preserving SDK defaults."""
import subprocess,sys
path=sys.argv[1]
symbols=[]
for line in subprocess.check_output(['nm','-g','--defined-only',path],text=True).splitlines():
    parts=line.split()
    if len(parts)!=3:continue
    name=parts[2]
    if name.startswith('_ZTVN10__cxxabiv1') or (name.startswith('_ZSt') and '__throw_' in name):
        symbols.append(name)
if not symbols:raise SystemExit('missing SDK fallback runtime symbols')
subprocess.run(['objcopy',*[f'--localize-symbol={name}' for name in symbols],path],check=True)
print('LibJS: localized',len(symbols),'SDK fallback symbols; GCC provides real RTTI')
