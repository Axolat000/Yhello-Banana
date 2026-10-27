#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Load out-of-tree Driver SDK packages and a frozen v1 module in the real OS.
Tests QEMU FT232 control + actual bulk data both ways, AML laptop/PD telemetry,
package data and backward ABI compatibility. No physical RF validation.
"""
import argparse,functools,hashlib,json,os,socket,subprocess,sys,threading
from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--32',dest='bits32',action='store_true')
ap.add_argument('--firmware',choices=['bios','uefi'],default='bios')
ap.add_argument('--legacy',type=Path,help='also install a pre-change edu.bpk')
a=ap.parse_args()
if a.bits32 and a.firmware=='uefi':ap.error('UEFI requires 64-bit CPU')
out=root/'build/logs'/('driver-sdk-'+a.firmware+('-32' if a.bits32 else '-64'))
out.mkdir(parents=True,exist_ok=True)
packages=out/'packages';packages.mkdir(exist_ok=True)
for name in ['sdk-probe','usb-serial-demo']:
    subprocess.run(['make','-C','driver-sdk/examples/'+name],cwd=root,check=True)
    (packages/(name+'.bpk')).write_bytes((root/'driver-sdk/examples'/name/(name+'.bpk')).read_bytes())
# Build a module with a truly frozen v1 prefix; no current CRT or driver headers.
for arch,bits in [('i686','32'),('x86_64','64')]:
    obj=out/(arch+'.o');binary=packages/('driver-'+arch)
    flags=['-m'+bits,'-O2','-ffreestanding','-fno-stack-protector','-fpie','-mgeneral-regs-only','-fno-asynchronous-unwind-tables']
    if bits=='64':flags+=['-mno-red-zone']
    subprocess.run(['gcc',*flags,'-c','tools/tests/driver_v1_probe.c','-o',str(obj)],cwd=root,check=True)
    subprocess.run(['ld','-m','elf_i386' if bits=='32' else 'elf_x86_64','-pie','--no-dynamic-linker',
                    '-z','noexecstack','-z','noseparate-code','-z','max-page-size=4096','--hash-style=sysv',
                    '-e','_banana_driver_start','-o',str(binary),str(obj)],cwd=root,check=True)
manifest=packages/'manifest'
manifest.write_text('name=driver-v1-probe\ntitle=Frozen driver ABI v1 probe\nversion=1.0\ntype=driver\n')
subprocess.run([sys.executable,'sdk/tools/bpkg','pack','-o',str(packages/'driver-v1-probe.bpk'),str(manifest),
                str(packages/'driver-i686'),str(packages/'driver-x86_64')],cwd=root,check=True)
subprocess.run([sys.executable,'sdk/tools/bpkg','check',str(packages/'driver-v1-probe.bpk')],cwd=root,check=True)
if a.legacy:(packages/'edu.bpk').write_bytes(a.legacy.read_bytes())
class Quiet(SimpleHTTPRequestHandler):
    def log_message(self,*args):pass
http=ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Quiet,directory=str(packages)))
threading.Thread(target=http.serve_forever,daemon=True).start()
backend=socket.socket();backend.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
backend.bind(('127.0.0.1',0));backend.listen();backend.settimeout(180)
backend_port=backend.getsockname()[1]
done=threading.Event();wire=[];errors=[]
def serial_backend():
    try:
        conn,_=backend.accept()
        with conn:
            conn.settimeout(180);data=b''
            while b'BDK USB ready\n' not in data:
                part=conn.recv(256)
                if not part:raise RuntimeError('USB backend closed before test message')
                data+=part
            wire.append(data);conn.sendall(b'BDK USB reply\n');done.wait(300)
    except Exception as ex:errors.append(str(ex))
threading.Thread(target=serial_backend,daemon=True).start()
prefix=out/'laptop'
subprocess.run(['iasl','-p',str(prefix),'tools/tests/laptop.asl'],cwd=root,check=True)
mailbox=out/'mailbox';mailbox.write_bytes(bytes(4096))
qemu=['-accel','tcg','-m','512M','-smp','2','-cdrom','Banana_OS.iso',
      '-nic','user,model=e1000','-device','edu,addr=0x5','-device','qemu-xhci,id=sdkxhci,addr=0x6',
      '-chardev',f'socket,id=usbdemo,host=127.0.0.1,port={backend_port},reconnect=1',
      '-device','usb-serial,chardev=usbdemo,bus=sdkxhci.0,id=sdkserial',
      '-acpitable','file='+str(prefix.with_suffix('.aml')),
      '-object','memory-backend-file,id=ucsimem,size=4096,mem-path='+str(mailbox)+',share=on',
      '-device','ivshmem-plain,memdev=ucsimem,addr=0x4']
if a.bits32:qemu+=['-cpu','qemu32']
if a.firmware=='uefi':
    variables=out/'OVMF_VARS.fd'
    variables.write_bytes(Path(os.environ.get('OVMF_VARS','/usr/share/OVMF/OVMF_VARS_4M.fd')).read_bytes())
    qemu+=['-fw_cfg','name=opt/ovmf/X-PciMmio64Mb,string=0','-drive',
           'if=pflash,format=raw,readonly=on,file='+os.environ.get('OVMF_CODE','/usr/share/OVMF/OVMF_CODE_4M.fd'),
           '-drive','if=pflash,format=raw,file='+str(variables)]
commands=['#wait 5','laptop ## 50%, discharging']
for name in ['driver-v1-probe','sdk-probe','usb-serial-demo']+(['edu'] if a.legacy else []):
    commands += [f'curl -o /tmp/{name}.bpk http://10.0.2.2:{http.server_port}/{name}.bpk',
                 f'pkg install /tmp/{name}.bpk'+(' && sleep 12' if name=='edu' else '')]
commands+=['#wait 4','drivers ## usb-serial-demo','lsusb ## driver-module','selftest boot ## 0 failed']
command_file=out/'commands.txt';command_file.write_text('\n'.join(commands)+'\n')
with (root/'Banana_OS.iso').open('rb') as iso:digest=hashlib.file_digest(iso,'sha256').hexdigest()
(out/'provenance.json').write_text(json.dumps({'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
    'iso_sha256':digest,'bits':32 if a.bits32 else 64,'firmware':a.firmware,'qemu_args':qemu,
    'packages':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in packages.glob('*.bpk')}},indent=2)+'\n')
try:
    result=subprocess.run([sys.executable,'tools/serialtest.py','--timeout','180','--boot-timeout','180',
        '--log',str(out/'serial.log'),'--cmds-file',str(command_file),'--',*qemu],cwd=root).returncode
finally:
    done.set();http.shutdown();backend.close()
text=(out/'serial.log').read_text(errors='replace')
for marker in ['driver-v1: ABI compatible','sdk-probe: firmware/package read passed',
               'sdk-probe: optional NET/WIFI/USB/POWER/FIRMWARE services ready',
               'sdk-probe: battery 50%, brightness 60%','sdk-probe: PD contract 20000 mV 3000 mA',
               'usb-serial-demo: control passed','usb-serial-demo: TX bytes verified','usb-serial-demo: RX bytes verified']:
    if marker not in text:errors.append('missing marker: '+marker)
if not wire or b'BDK USB ready\n' not in wire[0]:errors.append('host did not receive USB TX message')
if errors:raise RuntimeError('; '.join(errors))
sys.exit(result)
