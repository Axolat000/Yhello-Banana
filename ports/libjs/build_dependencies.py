#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build ICU and the small parser/BigInt dependencies against SDK headers."""
from pathlib import Path
import concurrent.futures,hashlib,json,os,subprocess,sys
here=Path(__file__).resolve().parent;root=here.parent.parent
out=here/'build/banana';src=here/'src';(out/'deps').mkdir(parents=True,exist_ok=True)
version=subprocess.check_output(['g++','-dumpversion'],text=True).strip().split('.')[0]
triple=subprocess.check_output(['g++','-dumpmachine'],text=True).strip()
cxx=['-nostdinc','-nostdinc++']
for directory in ['/usr/include/c++/'+version,'/usr/include/'+triple+'/c++/'+version,
                  str(root/'sdk/include'),str(root/'ports/llvm/include'),
                  subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()]:
    cxx+=['-isystem',directory]
cxx+=['-include',str(here/'include/libjs_compat.h'),'-std=c++17']
base=['-O1','-fPIC','-ffunction-sections','-fdata-sections','-fno-exceptions',
      '-mno-red-zone','-fno-stack-protector','-march=x86-64','-mtune=generic',
      '-mtls-dialect=gnu','-D__BANANA_OS__']
cflags=['-nostdinc','-isystem',str(root/'sdk/include'),'-isystem',
        subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()]
icu=src/'icu/source'
groups={}
for part in ['common','i18n']:
    extra=['-I'+str(icu/'common'),'-I'+str(icu/'i18n'),'-DU_STATIC_IMPLEMENTATION',
           '-DUCONFIG_NO_FILE_IO=1','-DU_ENABLE_DYLOAD=0','-DU_HAVE_MMAP=0',
           '-DU_COMMON_IMPLEMENTATION' if part=='common' else '-DU_I18N_IMPLEMENTATION']
    groups['icu'+part]=[(p,['g++',*cxx,*base,*extra]) for p in sorted((icu/part).glob('*.cpp'))]
groups['simdutf']=[(src/'simdutf-7.5.0/src/simdutf.cpp',
    ['g++',*cxx,*base,'-I'+str(src/'simdutf-7.5.0/include'),'-I'+str(src/'simdutf-7.5.0/src')])]
groups['simdjson']=[(src/'simdjson-4.2.4/src/simdjson.cpp',
    ['g++',*cxx,*base,'-I'+str(src/'simdjson-4.2.4/include'),'-I'+str(src/'simdjson-4.2.4/src')])]
groups['tommath']=[(p,['gcc',*cflags,*base,'-I'+str(src/'libtommath-1.3.0')])
    for p in sorted((src/'libtommath-1.3.0').glob('bn_*.c'))]
# Cache keys include declaration headers and compiler identity, not just sources.
header_hash=hashlib.sha256(subprocess.check_output(['g++','--version']))
for base_dir in [root/'sdk/include',root/'ports/llvm/include',here/'include',
                 icu/'common',icu/'i18n',src/'simdutf-7.5.0/include',
                 src/'simdutf-7.5.0/src',src/'simdjson-4.2.4/include',
                 src/'simdjson-4.2.4/src',src/'libtommath-1.3.0']:
    for path in sorted(base_dir.rglob('*')):
        if path.is_file() and path.suffix in ['.h','.inc']:
            header_hash.update(str(path).encode());header_hash.update(path.read_bytes())
header_stamp=header_hash.hexdigest()
wanted=sys.argv[1:] or list(groups)
jobs=[(group,p,flags) for group in wanted for p,flags in groups[group]]
def build(job):
    group,p,flags=job
    target=out/'deps'/(group+'_'+p.stem+'.o')
    cmd=[*flags,'-c',str(p),'-o',str(target)]
    sig=hashlib.sha256((json.dumps(cmd)+header_stamp+p.read_text()).encode()).hexdigest()
    stamp=target.with_suffix('.signature')
    if target.exists() and stamp.exists() and stamp.read_text()==sig:return group,target,0,''
    result=subprocess.run(cmd,capture_output=True,text=True)
    (out/'deps'/(group+'_'+p.stem+'.log')).write_text(result.stderr)
    if not result.returncode:stamp.write_text(sig)
    return group,target,result.returncode,result.stderr
objects={g:[] for g in wanted};failed=[]
with concurrent.futures.ThreadPoolExecutor(int(os.environ.get('JOBS','2'))) as pool:
    for n,(group,target,rc,err) in enumerate(pool.map(build,jobs),1):
        if rc:
            failed.append(str(target))
            print('FAILED',target.name,err[-2500:],flush=True)
        else:objects[group].append(str(target))
        if not n%25:print('dependencies',n,'/',len(jobs),flush=True)
if failed:raise SystemExit(str(len(failed))+' dependency failures')
for group,files in objects.items():
    subprocess.run(['ar','rcs',str(out/'deps'/('lib'+group+'.a')),*files],check=True)
if 'icucommon' in wanted:
    assembly=out/'deps/icudata.S'
    assembly.write_text('.section .rodata\n.balign 16\n.globl icudt76_dat\n.type icudt76_dat,@object\nicudt76_dat:\n.incbin "'+
        str(icu/'data/in/icudt76l.dat')+'"\n.size icudt76_dat,.-icudt76_dat\n.section .note.GNU-stack,"",@progbits\n')
    subprocess.run(['gcc','-c',str(assembly),'-o',str(out/'deps/icudata.o')],check=True)
    subprocess.run(['ar','rcs',str(out/'deps/libicudata.a'),str(out/'deps/icudata.o')],check=True)
print('SDK dependencies built:',', '.join(wanted),flush=True)
