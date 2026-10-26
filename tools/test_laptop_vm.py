#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native laptop firmware tests; no physical charging or connector validation."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--firmware',choices=['bios','uefi'],default='bios')
ap.add_argument('--32',dest='bits32',action='store_true')
a=ap.parse_args()
if a.bits32 and a.firmware=='uefi':ap.error('64-bit UEFI firmware requires a 64-bit CPU')
out=root/'build/logs'/('laptop-'+a.firmware+('-32' if a.bits32 else '-64'))
out.mkdir(parents=True,exist_ok=True)
prefix=out/'laptop'
subprocess.run(['iasl','-p',str(prefix),'tools/tests/laptop.asl'],cwd=root,check=True)
mailbox=out/'mailbox';mailbox.write_bytes(bytes(4096))
qemu=['-accel','tcg','-cdrom','Banana_OS.iso','-m','512M','-smp','4','-nic','none',
      '-acpitable','file='+str(prefix.with_suffix('.aml')),
      '-object','memory-backend-file,id=ucsimem,size=4096,mem-path='+str(mailbox)+',share=on',
      '-device','ivshmem-plain,memdev=ucsimem,addr=0x4']
if a.bits32:qemu+=['-cpu','qemu32']
if a.firmware=='uefi':
    # QEMU's i440fx DSDT uses 32-bit AML, so the synthetic dynamic BASE()
    # cannot encode a BAR above 4 GiB. Keep this fixture's BAR below 4 GiB.
    # Native 64-bit ACPI resource bounds are covered by test_power_host.py.
    qemu+=['-fw_cfg','name=opt/ovmf/X-PciMmio64Mb,string=0']
    code=Path(os.environ.get('OVMF_CODE','/usr/share/OVMF/OVMF_CODE_4M.fd'))
    template=Path(os.environ.get('OVMF_VARS','/usr/share/OVMF/OVMF_VARS_4M.fd'))
    variables=out/'OVMF_VARS.fd';variables.write_bytes(template.read_bytes())
    qemu+=['-drive','if=pflash,format=raw,readonly=on,file='+str(code),
           '-drive','if=pflash,format=raw,file='+str(variables)]
commands=out/'commands.txt'
commands.write_text("""#wait 5
laptop ## 50%, discharging
usbc ## contract: 20000 mV, 3000 mA, 60000 mW
brightness ## 60%
brightness 80 ## 80%
#wait 3
brightness ## 80%
usbc role 1 source ## power-role request queued
#wait 3
usbc ## source (supplying power)
usbc role 1 sink ## power-role request queued
#wait 3
usbc ## sink (receiving power)
cat /usr/share/licenses/uacpi/LICENSE ## Daniil Tatianin
selftest boot ## 0 failed
""")
with (root/'Banana_OS.iso').open('rb') as iso:
    digest=hashlib.file_digest(iso,'sha256').hexdigest()
provenance={'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
            'iso_sha256':digest,'firmware':a.firmware,'bits':32 if a.bits32 else 64,'qemu_args':qemu,
            'fixture_sha256':hashlib.sha256((root/'tools/tests/laptop.asl').read_bytes()).hexdigest()}
(out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
sys.exit(subprocess.run([sys.executable,'tools/serialtest.py','--timeout','120','--boot-timeout','180',
                         '--log',str(out/'serial.log'),'--cmds-file',str(commands),'--',*qemu],cwd=root).returncode)
