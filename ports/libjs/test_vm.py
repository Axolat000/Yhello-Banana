#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test the optional package on a disposable FAT disk with the normal ISO."""
from pathlib import Path
import argparse,shutil,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--memory',default='512M')
parser.add_argument('--dom',action='store_true')
parser.add_argument('--uefi',action='store_true')
parser.add_argument('--google',action='store_true',help='diagnose unfinished Google compatibility')
parser.add_argument('--live',action='store_true',help='also fetch GitHub release pages')
parser.add_argument('--log',type=Path,default=root/'ports/libjs/build/banana/vm.serial')
args=parser.parse_args()
args.log=args.log.resolve();args.log.parent.mkdir(parents=True,exist_ok=True)
package=root/('apps/libjsworker/libjsworker-x86_64.bpk' if args.dom else 'apps/libjsprobe/libjsprobe-x86_64.bpk')
if not package.is_file():parser.error('build the optional target package first')
with tempfile.TemporaryDirectory(prefix='banana-libjs-') as temporary:
    work=Path(temporary);disk=work/'packages.img'
    with disk.open('wb') as f:f.truncate(128*1024*1024)
    subprocess.run(['mformat','-i',str(disk),'-F','::'],check=True)
    subprocess.run(['mcopy','-i',str(disk),str(package),'::/libjsprobe.bpk'],check=True)
    command=[sys.executable,str(root/'tools/serialtest.py'),'--log',str(args.log),
             '--boot-timeout','240','--timeout','240']
    for text in ['mount -a','pkg install /mnt/ide/libjsprobe.bpk ## installed',
                 ('selftest browser libjsdom ## 0 failed' if args.dom else 'selftest browser libjs ## 28 passed, 0 failed'),
                 'selftest browser isolation ## 26 passed, 0 failed',
                 'selftest browser ## 0 failed','selftest memory ## 0 failed']:
        command+=['--cmd',text]
    if args.live:
        command+=['--cmd','selftest browser github ## 0 failed']
        if args.dom:command+=['--cmd','selftest browser libjsgithub ## 0 failed']
    if args.google:command+=['--cmd','selftest browser libjsgoogle ## 0 failed']
    command+=['--','-accel','tcg','-m',args.memory,'-smp','2',
              '-nic','user,model=e1000' if (args.live or args.google) else 'none',
              '-cdrom',str(root/'Banana_OS.iso'),'-boot','d',
              '-drive','file='+str(disk)+',format=raw,if=ide']
    if args.uefi:
        shutil.copy2('/usr/share/OVMF/OVMF_VARS_4M.fd',work/'vars.fd')
        command+=['-drive','if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd',
                  '-drive','if=pflash,format=raw,file='+str(work/'vars.fd')]
    subprocess.run(command,cwd=root,check=True)
