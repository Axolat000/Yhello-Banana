#!/usr/bin/env python3
"""Inject errors into Bash's actual post-ENOEXEC binary/script probe."""
import os, shlex, subprocess, sys
from pathlib import Path
src=Path(sys.argv[1])
root=Path(__file__).resolve().parents[1]
out=root/'build/test/bash-exec';out.mkdir(parents=True,exist_ok=True)
s=(src/'execute_cmd.c').read_text()
a=s.index('#define READ_SAMPLE_BUF(');b=s.index('/* Call execve',a)
macro=s[a:b]
a=s.index('  /* This file is executable.')
a=s.index('  READ_SAMPLE_BUF (command, sample, sample_len);',a)
b=s.index('  reset_parser ();',a)
g=(src/'general.c').read_text();i=g.index('\ncheck_binary_file (');i=g.rfind('\nint',0,i)
j=g.index('\n/* ****',i)
(out/'bash_probe.inc').write_text(g[i:j]+'\n'+macro+
    '\nstatic int probe(void) {\nint fd,sample_len,i=8; char sample[128]; const char* command="/bin/probe";\n'+
    s[a:b]+'\nreturn 99;\n}\n')
exe=out/'test'
subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=gnu11','-O1','-Wall','-Wextra','-Werror',
    '-Wno-unused-parameter','-Wno-unused-function','-DHAVE_HASH_BANG_EXEC=1','-I'+str(out),'tools/tests/bash_exec.c','-o',str(exe)],cwd=root,check=True)
subprocess.run([str(exe)],cwd=root,check=True)
