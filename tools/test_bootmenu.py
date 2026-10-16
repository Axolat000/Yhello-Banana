#!/usr/bin/env python3
"""Check the shared firmware boot menu renderer and export previews without QEMU."""
from pathlib import Path
import argparse
import struct
import subprocess
import zlib
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--32',dest='bits32',action='store_true')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
out=root/'build/test/boot';out.mkdir(parents=True,exist_ok=True)
flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Iweb/test/shim','-Ikernel']
if a.bits32:flags+=['-m32']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
binary=out/('test32' if a.bits32 else 'test64')
subprocess.run(['cc',*flags,'tools/tests/bootmenu.c','-o',binary],cwd=root,check=True)
subprocess.run([binary],cwd=root,check=True)
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
for w,h,selected,seconds in [(640,480,8,-1),(1024,768,0,3),(1920,1080,2,1)]:
    ppm=out/f'menu-{w}x{h}.ppm'
    subprocess.run([binary,ppm,str(w),str(h),str(selected),str(seconds)],cwd=root,check=True)
    with ppm.open('rb') as f:
        assert f.readline()==b'P6\n';assert f.readline()==f'{w} {h}\n'.encode();assert f.readline()==b'255\n';pixels=f.read()
    rows=b''.join(b'\0'+pixels[y*w*3:(y+1)*w*3] for y in range(h))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
    ppm.with_suffix('.png').write_bytes(png)
print('Boot menu previews:',out)
