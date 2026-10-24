#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Recompile a configured LibJS reference against the Banana SDK."""
from pathlib import Path
import concurrent.futures, hashlib, json, os, shlex, subprocess, sys
here=Path(__file__).resolve().parent
root=here.parent.parent
ref=Path(sys.argv[1]).resolve()
out=Path(sys.argv[2]).resolve()
(out/'obj').mkdir(parents=True,exist_ok=True)
(out/'logs').mkdir(exist_ok=True)
version=subprocess.check_output(['g++','-dumpversion'],text=True).strip().split('.')[0]
triple=subprocess.check_output(['g++','-dumpmachine'],text=True).strip()
flags=['-nostdinc','-nostdinc++']
for directory in ['/usr/include/c++/'+version,'/usr/include/'+triple+'/c++/'+version,
                  str(root/'sdk/include'),str(root/'ports/llvm/include'),
                  subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()]:
    flags+=['-isystem',directory]
flags+=['-I'+str(out/'include'),'-include',str(here/'include/libjs_compat.h'),
        '-D__BANANA_OS__','-mno-red-zone','-mtls-dialect=gnu','-fno-stack-protector',
        '-fno-asynchronous-unwind-tables','-fdiagnostics-color=never']
entries=json.loads((ref/'compile_commands.json').read_text())
all_entries=entries
if os.environ.get('LIBJS_CORE_ONLY')=='1':
    entries=[e for e in entries if '/LibJS/' in e['file'] or '/AK/' in e['file'] or e['file'].endswith('/probe.cpp')]
if os.environ.get('LIBJS_DEPS_ONLY')=='1':
    entries=[e for e in entries if '/LibJS/' not in e['file'] and '/AK/' not in e['file'] and not e['file'].endswith('/probe.cpp')]
    entries=[e for e in entries if '/LibCrypto/' not in e['file'] or '/BigInt/' in e['file'] or '/BigFraction/' in e['file']]
headers=hashlib.sha256()
source=Path(next(e['file'] for e in all_entries if '/AK/' in e['file'])).parent
header_bases=[root/'sdk/include',root/'ports/llvm/include',here/'include',out/'include',ref/'AK',ref/'Libraries',source]
header_bases += [source.parent/'Libraries'/name for name in ['LibCore','LibFileSystem','LibGC','LibJS','LibCrypto','LibUnicode','LibRegex','LibSyntax','LibURL','LibTextCodec']]
headers.update(subprocess.check_output(['g++','--version']))
for base in header_bases:
    for path in sorted(base.rglob('*')):
        if path.is_file() and path.suffix in ['.h','.inc']:
            headers.update(str(path).encode());headers.update(path.read_bytes())
header_hash=headers.hexdigest()
def compile_one(e):
    args=shlex.split(e['command'])
    while Path(args[0]).name in ['ccache','sccache']:args.pop(0)
    replacements={'FileWatcherInotify.cpp':'FileWatcherUnimplemented.cpp',
                  'TimeZoneWatcherUnix.cpp':'TimeZoneWatcherUnimplemented.cpp',
                  'ProcessStatisticsLinux.cpp':'ProcessStatisticsUnimplemented.cpp'}
    original=Path(e['file'])
    actual=original.with_name(replacements[original.name]) if original.name in replacements else original
    args=[str(actual) if x==str(original) else x for x in args]
    obj=out/'obj'/args[args.index('-o')+1].replace('/','_')
    args[args.index('-o')+1]=str(obj)
    command=[args[0],*flags,*args[1:],'-O1','-march=x86-64','-mtune=generic','-fno-stack-protector','-fdiagnostics-color=never']
    if original.name=='JSONObject.cpp':
        # Amalgamated simdjson exposes intentionally dynamic format wrappers.
        command+=['-Wno-error=format-nonliteral']
    specific=(here/'probe.h').read_text() if e['file']==str(here/'probe.cpp') else ''
    signature=hashlib.sha256((json.dumps(command)+header_hash+specific).encode()).hexdigest()
    stamp=obj.with_suffix('.signature')
    if obj.exists() and stamp.exists() and stamp.read_text()==signature and obj.stat().st_mtime>=actual.stat().st_mtime:
        return e['file'],0,'cached'
    result=subprocess.run(command,cwd=e['directory'],capture_output=True,text=True)
    (out/'logs'/(obj.stem+'.log')).write_text(result.stderr)
    if not result.returncode:stamp.write_text(signature)
    return e['file'],result.returncode,result.stderr
failed=[]
with concurrent.futures.ThreadPoolExecutor(int(os.environ.get('JOBS','2'))) as pool:
    for i,(name,rc,err) in enumerate(pool.map(compile_one,entries),1):
        if rc:
            failed.append(name)
            print('FAILED',name,':','; '.join(line.strip() for line in err.splitlines() if 'error:' in line)[:450],flush=True)
        elif i%25==0:print('compiled',i,'/',len(entries),flush=True)
(out/'failed.json').write_text(json.dumps(failed,indent=2))
print('SDK compilation:',len(entries)-len(failed),'succeeded;',len(failed),'failed',flush=True)
sys.exit(bool(failed))
