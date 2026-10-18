#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,struct,sys,os
root=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve(); info=json.loads((out/'selected.json').read_text());sdk=Path(os.environ.get('LLVM_SDK_OBJECTS',str(root/'ports/mesa/gl3test/build/x86_64')))
if not list(sdk.glob('*.o')):raise SystemExit('Build ports/mesa/gl3test first to provide the SDK import ABI')
def run(a,**k):return subprocess.run(a,check=True,**k)
(out/'lib').mkdir(exist_ok=True)
for a,objs in info['archives'].items():
 target=out/'lib'/Path(a).name
 if target.exists():target.unlink()
 run(['ar','rcs',str(target)]+[str(out/'obj'/o.replace('/','_')) for o in objs])
symbols={}
for p in sdk.glob('*.o'):
 if p.name in ['__exports.o','gl3test.o','sdk_crt0.o']:continue
 for line in subprocess.check_output(['nm','-g','--defined-only',str(p)],text=True).splitlines():
  x=line.split()
  if len(x)==3 and not x[2].startswith('_ZTVN10__cxxabiv1'):symbols[x[2]]=x[1]
s='\n'.join('.globl '+n+'\n.type '+n+(',@function\n'+n+': ret' if t.upper() in ['T','W'] else ',@object\n.size '+n+',8\n'+n+': .quad 0') for n,t in sorted(symbols.items()))+'\n.section .note.GNU-stack,"",@progbits\n'
(out/'imports.S').write_text(s)
run(['gcc','-shared','-nostdlib','-Wl,-soname,libbanana-import.so','-o',str(out/'libbanana-import.so'),str(out/'imports.S')])
# This link-only symbol provider prevents native libstdc++ from importing
# a second allocator/runtime. It is NEVER deployed. DT_NEEDED is removed
# after linking, leaving normal undefined symbols for the SDK's loader.
a=['g++','-shared','-nostdlib','-Wl,--hash-style=both','-Wl,-soname,libLLVM.so','-o',str(out/'libLLVM.so'),'-Wl,--no-as-needed',str(out/'libbanana-import.so')]
compat=out/'compat.o'
subprocess.run(['gcc','-c','-fPIC','-O2','-mno-red-zone','-nostdinc','-isystem',str(root/'sdk/include'),'-isystem',str(root/'ports/llvm/include'),'-isystem',subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip(),str(root/'ports/llvm/compat.c'),'-o',str(compat)],check=True)
version=subprocess.check_output(['g++','-dumpversion'],text=True).strip().split('.')[0]
triple=subprocess.check_output(['g++','-dumpmachine'],text=True).strip()
cxxflags=['g++','-c','-fPIC','-O2','-mno-red-zone','-mtls-dialect=gnu','-fno-exceptions','-fno-rtti','-nostdinc','-nostdinc++']
for directory in ['/usr/include/c++/'+version,'/usr/include/c++/'+version+'/'+triple,'/usr/include/'+triple+'/c++/'+version,str(root/'sdk/include'),str(root/'ports/llvm/include'),subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()]:
 if Path(directory).is_dir():cxxflags+=['-isystem',directory]
run(cxxflags+['-include',str(root/'ports/llvm/include/cxx_compat.h'),str(root/'ports/llvm/runtime_test.cpp'),'-o',str(out/'runtime_test.o')])
a+=[subprocess.check_output(['g++','-print-file-name=crtbeginS.o'],text=True).strip(),str(compat),str(out/'runtime_test.o')]
a+=['-Wl,-u,'+x for x in info['roots']]
a+=['-Wl,--start-group']+[str(out/'lib'/Path(x).name) for x in info['archives']]
a+=[subprocess.check_output(['g++','-print-file-name='+x],text=True).strip() for x in ['libstdc++.a','libgcc.a','libgcc_eh.a']]+['-Wl,--end-group']
a+=[subprocess.check_output(['g++','-print-file-name=crtendS.o'],text=True).strip()]
run(a)
# Remove exactly the link-only provider, without truncating .dynamic or
# accepting an accidental host dependency. The SDK resolves these imports.
p=out/'libLLVM.so';b=bytearray(p.read_bytes())
phoff=struct.unpack_from('<Q',b,32)[0];phsize,phnum=struct.unpack_from('<HH',b,54)
segments=[struct.unpack_from('<IIQQQQQQ',b,phoff+i*phsize) for i in range(phnum)]
dynamic=next(ph for ph in segments if ph[0]==2)
entries=[(j,*struct.unpack_from('<QQ',b,j)) for j in range(dynamic[2],dynamic[2]+dynamic[5],16)]
straddr=next(v for _,t,v in entries if t==5)
load=next(ph for ph in segments if ph[0]==1 and ph[3]<=straddr<ph[3]+ph[5])
strings=load[2]+straddr-load[3]
removed=0
for offset,tag,value in entries:
 if tag==1:
  name=bytes(b[strings+value:]).split(b'\0',1)[0]
  if name!=b'libbanana-import.so':raise RuntimeError('Unexpected host dependency: '+str(name))
  struct.pack_into('<QQ',b,offset,21,0);removed+=1
if removed!=1:raise RuntimeError('Missing link-only SDK symbol provider')
p.write_bytes(b)
subprocess.run([sys.executable,str(root/'tools/check_shared_imports.py'),str(p),*[str(x) for x in sdk.glob('*.o')]],check=True)
print('Linked '+str(p))
