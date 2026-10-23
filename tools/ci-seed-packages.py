#!/usr/bin/env python3
"""Seed embedded application packages from a verified release for kernel boot CI.

Only compressed application/example packages and their indexes are reused.
Kernel C/assembly and boot loaders are rebuilt from the checkout. This is a
kernel regression build, not application/SDK build qualification.
"""
from pathlib import Path
import re, struct, subprocess, sys, tempfile, zlib
root=Path(__file__).resolve().parents[1]
iso=Path(sys.argv[1]).resolve()

def elf_blob(file):
    b=file.read_bytes()
    assert b[:4]==b'\x7fELF' and b[5]==1, 'little-endian ELF required'
    if b[4]==2:
        phoff=struct.unpack_from('<Q',b,32)[0]
        size,count=struct.unpack_from('<HH',b,54)
        fmt='<IIQQQQQQ'
    else:
        phoff=struct.unpack_from('<I',b,28)[0]
        size,count=struct.unpack_from('<HH',b,42)
        fmt='<IIIIIIII'
    segments=[]
    for i in range(count):
        p=struct.unpack_from(fmt,b,phoff+i*size)
        if p[0]!=1:continue
        offset,address,length=(p[2],p[3],p[5]) if b[4]==2 else (p[1],p[2],p[4])
        segments.append((address,address+length,offset))
    symbols={}
    for line in subprocess.check_output(['nm','--defined-only',str(file)],text=True).splitlines():
        fields=line.split()
        if len(fields)==3:
            symbols[fields[2]]=int(fields[0],16)
    def extract(symbol):
        start,end=symbols[symbol],symbols[symbol+'_end']
        assert end>start,(symbol,start,end)
        for lo,hi,offset in segments:
            if lo<=start and end<=hi:return b[offset+start-lo:offset+end-lo]
        raise ValueError('symbol outside file-backed segment: '+symbol)
    return extract

def put(path,content):
    target=root/path
    target.parent.mkdir(parents=True,exist_ok=True)
    if path.endswith(".bpk.z"):
        target.with_suffix("").write_bytes(zlib.decompress(content))
    target.write_bytes(content)
    print('seed',path,len(content))

with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    kernels={}
    for arch,name in [('i686','kernel.bin'),('x86_64','kernel64.bin')]:
        target=tmp/name
        subprocess.run(['xorriso','-osirrox','on','-indev',str(iso),'-extract','/boot/'+name,str(target)],check=True)
        kernels[arch]=elf_blob(target)
    assembly=(root/'kernel/appbin.asm').read_text()
    for symbol,path in re.findall(r'(\w+):\s*incbin\s+"([^"]+)"',assembly):
        if not path.endswith('.bpk.z'):continue
        arch='i686' if '-i686.' in path else 'x86_64'
        put(path,kernels[arch](symbol))
    for symbol,path in re.findall(r'(\w+):\s*incbin\s+"([^"]+)"',(root/'kernel/exbin.asm').read_text()):
        put(path,kernels['x86_64'](symbol))
    make=(root/'Makefile').read_text()
    for variable in ['PORTS64','OPTPORTS64','APPS64']:
        names=re.search(r'^'+variable+r'\s*=\s*([^\n]+)',make,re.M)[1].split()
        for name in names:
            path=f'apps/{name}/{name}-x86_64.bpk.z' if variable=='APPS64' else f'ports/{name}/build/{name}-x86_64.bpk.z'
            put(path,kernels['x86_64']('port_'+name))
    # Write indexes after packages so make will retain their matching metadata.
    for arch in kernels:put(f'build/builtin-meta-{arch}.bin',kernels[arch]('builtin_meta'))
