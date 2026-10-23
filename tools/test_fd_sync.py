#!/usr/bin/env python3
"""Deterministic host regressions for the actual Banana OS implementation."""
import argparse, os, shlex, subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--32', dest='bits32', action='store_true')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
out=root/'build/test/fd_sync'
out.mkdir(parents=True,exist_ok=True)
def function(path, signature):
    s=(root/path).read_text()
    start=s.index(signature)
    opening=s.index('{',start)
    depth=1
    end=opening+1
    while depth:
        depth += (s[end]=='{')-(s[end]=='}')
        end+=1
    return s[start:end]+'\n'
(out/'sync_entry.inc').write_text(function('kernel/app.c','static int a_fd_sync('))
(out/'sync_backend.inc').write_text(function('kernel/fsdisk.c','static int sync_disk('))
(out/'sync_sdk.inc').write_text(function('sdk/lib/unix.c','int fsync(')+function('sdk/lib/unix.c','int fdatasync('))
api=(root/'sdk/include/banana_api.h').read_text()
assert api.index('(*fd_sync)') > api.index('(*poll_mask)')
app=(root/'kernel/app.c').read_text()
assert 'SPEC(fd_sync, "i", 0)' in app and 'g_api.fd_sync = G(a_fd_sync)' in app

flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror', '-Ikernel','-Inet', '-I'+str(out),'-include','web/test/shim/types.h']
if a.bits32: flags+=['-m32']
if a.sanitize: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
binary=out/('test.exe' if os.name=='nt' else 'test')
subprocess.run([*shlex.split(os.environ.get('CC','cc')),*flags,*['tools/tests/fd_sync.c'],'-o',str(binary)],cwd=root,check=True)
subprocess.run([str(binary)],cwd=root,check=True)
