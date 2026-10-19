#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check that a port's strong ELF imports exist in its SDK/provider objects."""
import argparse, subprocess, sys
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('library');ap.add_argument('providers',nargs='+');a=ap.parse_args()
def symbols(path, defined):
 args=['nm']+(['-D'] if '.so' in Path(path).name else ['-g'])
 args+=['--defined-only' if defined else '--undefined-only',path]
 result=set()
 for line in subprocess.check_output(args,text=True).splitlines():
  parts=line.split()
  if len(parts)>=2 and (defined or parts[-2]=='U'): result.add(parts[-1].split('@')[0])
 return result
available=set().union(*(symbols(p,True) for p in a.providers))
missing=symbols(a.library,False)-available
if missing:
 print('Missing strong imports in '+a.library+':\n  '+'\n  '.join(sorted(missing)));sys.exit(1)
print('Shared imports resolve: '+a.library)
