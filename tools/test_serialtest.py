#!/usr/bin/env python3
"""Run the real serial harness against a fake QEMU serial peer."""
from pathlib import Path
import os, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
peer='''#!/usr/bin/env python3
import socket,sys,re
port=int(re.search(r'port=(\\d+)',sys.argv[sys.argv.index('-chardev')+1])[1])
with socket.socket() as s:
    s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    s.bind(('127.0.0.1',port));s.listen(1)
    c,_=s.accept()
    with c:
        c.sendall(b'banana@banana-os:~$ ')
        line=b'';confirm=False
        while True:
            ch=c.recv(1)
            if not ch:break
            if ch not in (b'\\r',b'\\n'):line+=ch;continue
            text=line.decode();line=b''
            c.sendall((text+'\\r\\n').encode())
            if text=='install':
                confirm=True;c.sendall(b'Continue? [y/N] ');continue
            if confirm:
                assert text=='y';confirm=False;c.sendall(b'install: done\\r\\n')
            elif text=='bad':c.sendall(b'PANIC: injected failure\\r\\n')
            else:c.sendall(b'expected output\\r\\n')
            c.sendall(b'banana@banana-os:~$ ')
'''
with tempfile.TemporaryDirectory() as tmp:
    fake=Path(tmp)/'fake-qemu';fake.write_text(peer);fake.chmod(0o755)
    base=[sys.executable,str(root/'tools/serialtest.py'),'--qemu',str(fake),
          '--log',str(Path(tmp)/'serial.log'),'--timeout','4','--boot-timeout','4']
    p=subprocess.run([*base,'--cmd','install ##? Continue? [y/N]',
                      '--cmd','y ## install: done','--cmd','echo ## expected output'],capture_output=True,text=True)
    if p.returncode: print(p.stdout,p.stderr);raise SystemExit(p.returncode)
    p=subprocess.run([*base,'--cmd','bad'],capture_output=True,text=True)
    assert p.returncode==1 and 'PANIC' in p.stdout,(p.returncode,p.stdout,p.stderr)
print('serialtest: interactive confirmation, next answer, normal expectation and panic detection passed')
