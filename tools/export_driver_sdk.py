#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export a Driver SDK independent from application headers/build rules."""
from pathlib import Path
import argparse,tarfile
root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument("-o","--output",type=Path,default=root/"banana-driver-sdk.tar.gz")
a=ap.parse_args()
with tarfile.open(a.output,"w:gz") as archive:
    for path in sorted((root/"driver-sdk").rglob("*")):
        rel=path.relative_to(root/"driver-sdk")
        if any(p in ("build","__pycache__") for p in rel.parts) or path.suffix in (".bpk",".o",".d",".pyc"):
            continue
        if path.is_file():
            source=root/"sdk/tools/bpkg" if str(rel)=="tools/bpkg" else path
            archive.add(source,arcname="banana-driver-sdk/"+str(rel),recursive=False)
print(a.output)
