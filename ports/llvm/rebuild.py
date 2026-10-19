#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Cross-compile the LLVM X86 JIT closure used by a native Mesa reference."""
from pathlib import Path
import os,sys,json,shlex,subprocess,concurrent.futures,re,hashlib
here=Path(__file__).resolve().parent;root=here.parent.parent
ref=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();mesa=Path(sys.argv[3]).resolve()
(out/'obj').mkdir(parents=True,exist_ok=True);(out/'logs').mkdir(exist_ok=True)
config=out/'include/llvm/Config/config.h';config.parent.mkdir(parents=True,exist_ok=True)
s=(ref/'include/llvm/Config/config.h').read_text()
for name in ['HAVE_MALLINFO','HAVE_MALLINFO2','HAVE_DECL_ARC4RANDOM','HAVE_FUTIMENS','HAVE_BACKTRACE','HAVE_SBRK']:
 s=re.sub(r'^#define '+name+r' .*$', '#undef '+name, s, flags=re.M)
config.write_text(s)
def ninja(*a):return subprocess.check_output(['ninja','-C',str(ref),'-t',*a],text=True)
commands=ninja('commands','libLLVM.so').splitlines();line=next(l for l in reversed(commands) if '-shared' in l and '.so' in l)
args=shlex.split(line);archives=[x for x in args if x.endswith('.a') and not x.startswith('/')]
def inputs(target):
 s=ninja('query',target);ins=re.search(r'input: \S+\n((?:    .*\n)*)',s)
 return [x.strip() for x in ins[1].splitlines() if x.strip().endswith('.o') and not x.strip().startswith('|')]
members={a:inputs(a) for a in archives};direct=[x for x in args if x.endswith('.o')]
# Select the static LLVM object closure actually needed by this Mesa build.
# This keeps the SDK port to X86 JIT support, without unrelated LLVM tools.
def names(path,defined):
 data=subprocess.check_output(['nm','-D','--defined-only' if defined else '--undefined-only',str(path)],text=True)
 return {x[-1].split('@')[0] for l in data.splitlines() if len(x:=l.split())>=2 and (defined or x[-2]=='U')}
native=next((ref/'lib').glob('libLLVM.so.*'))
roots=sorted(names(mesa,False)&names(native,True))
if not roots:raise RuntimeError('Mesa reference does not import LLVM')
link=['g++','-shared','-Wl,-Map,'+str(out/'native-map.txt'),'-o',str(out/'native-subset.so')]
link+=['-Wl,-u,'+x for x in roots]+['-Wl,--start-group']+[str(ref/x) for x in archives]+['-Wl,--end-group','-lpthread','-ldl','-lm','-lrt']
subprocess.run(link,check=True)
maptext=(out/'native-map.txt').read_text().split('Discarded input sections')[0]
chosen={a:[o for o in objs if str(ref/a)+'('+Path(o).name+')' in maptext] for a,objs in members.items()}
selected={'roots':roots,'archives':{a:v for a,v in chosen.items() if v}}
(out/'selected.json').write_text(json.dumps(selected))
members=selected['archives'];direct=[]
wanted=set(o for v in members.values() for o in v)
entries=json.loads((ref/'compile_commands.json').read_text());byout={shlex.split(e['command'])[shlex.split(e['command']).index('-o')+1]:e for e in entries}
version=subprocess.check_output(['g++','-dumpversion'],text=True).strip().split('.')[0];triple=subprocess.check_output(['g++','-dumpmachine'],text=True).strip()
base=['-nostdinc','-isystem',str(root/'sdk/include'),'-isystem',str(root/'ports/llvm/include'),'-isystem',subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip(),'-I'+str(out/'include'),'-D__BANANA_OS__','-mno-red-zone','-mtls-dialect=gnu','-fno-stack-protector']
cxx=['-nostdinc++','-isystem','/usr/include/c++/'+version,'-isystem','/usr/include/c++/'+version+'/'+triple,'-isystem','/usr/include/'+triple+'/c++/'+version,'-include',str(here/'include/cxx_compat.h')]
headerhash=hashlib.sha256()
source=Path(re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$',(ref/'CMakeCache.txt').read_text(),re.M)[1])
for headerbase in [root/'sdk/include',here/'include',ref/'include',source/'include']:
 for path in sorted(headerbase.rglob('*')):
  if path.is_file() and path.suffix in ['.h','.inc']:
   headerhash.update(str(path).encode());headerhash.update(path.read_bytes())
headerstamp=headerhash.hexdigest()+subprocess.check_output(['g++','--version'],text=True).splitlines()[0]
def objname(o):return out/'obj'/o.replace('/','_')
def build(o):
 e=byout[o];target=objname(o)
 a=shlex.split(e['command'])
 while Path(a[0]).name in ['ccache','sccache']:a.pop(0)
 cpp=e['file'].endswith(('.cpp','.cc','.cxx'));pre=base[:1]+(cxx if cpp else [])+base[1:]
 res=[a[0],*pre];i=1
 while i<len(a):
  x=a[i]
  if x in ['-MD','-MMD','-funwind-tables']:i+=1;continue
  if x in ['-MF','-MT','-MQ']:i+=2;continue
  if x=='-o':res+=['-o',str(target)];i+=2;continue
  res.append(x);i+=1
 res+=['-O1','-fno-diagnostics-color','-fno-asynchronous-unwind-tables']
 signature=hashlib.sha256((headerstamp+json.dumps(res)).encode()).hexdigest();stamp=target.with_suffix('.signature')
 if os.environ.get('KEEP','1')=='1' and target.exists() and stamp.exists() and stamp.read_text()==signature and target.stat().st_mtime>=Path(e['file']).stat().st_mtime:return o,0,''
 r=subprocess.run(res,cwd=e['directory'],capture_output=True,text=True)
 if not r.returncode:stamp.write_text(signature)
 log=out/'logs'/(o.replace('/','_')+'.log')
 if r.returncode:log.write_text(r.stderr)
 elif log.exists():log.unlink()
 return o,r.returncode,r.stderr
print('LLVM library:',len(archives),'archives,',len(wanted),'objects',flush=True)
missing=wanted-byout.keys()
if missing:raise RuntimeError(missing)
failures=[]
with concurrent.futures.ThreadPoolExecutor(int(os.environ.get('JOBS','4'))) as pool:
 for number,(name,rc,err) in enumerate(pool.map(build,sorted(wanted)),1):
  if rc:
   failures.append(name);print('FAILED',name, ':', '; '.join(re.findall(r'(?:fatal )?error: (.*)',err)[:3]),flush=True)
  elif not number%50:print('compiled',number,'/',len(wanted),flush=True)
print('Errors:',len(failures),flush=True)
if failures:sys.exit(1)
(out/'inputs.json').write_text(json.dumps({'archives':members,'direct':direct,'args':args}))

subprocess.run([sys.executable,str(here/"link.py"),str(out)],check=True)
