#!/usr/bin/env python3
"""Exercise bounded SCSI discovery and FAT data I/O on disposable QEMU disks.

Build the current ISO first. This smoke test does not qualify installation or
power-cut durability. Images, commands, hashes and serial logs stay in build/test.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--transport', choices=['legacy', 'modern'], default='modern')
p.add_argument('--firmware', choices=['bios', 'uefi'], default='bios')
p.add_argument('--ovmf', default='/usr/share/OVMF/OVMF_CODE_4M.fd')
p.add_argument('--ovmf-vars', default='/usr/share/OVMF/OVMF_VARS_4M.fd')
a = p.parse_args()
out = root / 'build/test/virtio-scsi' / f'qemu-{a.transport}-{a.firmware}'
out.mkdir(parents=True, exist_ok=True)

def run(cmd):
    subprocess.run(cmd, cwd=root, check=True)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

source = out / 'source.bin'
source.write_bytes(bytes((i * 17 + i // 512) % 251 for i in range(512 * 519 + 73)))
images = []
for i in range(3):
    image = out / f'disk{i}.img'
    with image.open('wb') as f:
        f.truncate(128 * 1024 * 1024)
    run(['mformat', '-i', str(image), '-F', '::'])
    run(['mcopy', '-i', str(image), str(source), '::source.bin'])
    images.append(image)
before = [digest(image) for image in images]
transport = 'disable-modern=on' if a.transport == 'legacy' else 'disable-legacy=on'
args = ['-accel', 'tcg', '-m', '1024', '-smp', '4', '-nic', 'user,model=e1000',
        '-cdrom', str(root / 'Banana_OS.iso'), '-boot', 'd',
        '-device', f'virtio-scsi-pci,id=sc0,{transport}',
        '-device', f'virtio-scsi-pci,id=sc1,{transport}']
for i, (ctl, target, lun) in enumerate([(0, 0, 1), (0, 1, 0), (1, 2, 2)]):
    args += ['-drive', f'file={images[i]},if=none,id=d{i},format=raw,cache=writeback',
             '-device', f'scsi-hd,drive=d{i},bus=sc{ctl}.0,channel=0,scsi-id={target},lun={lun}']
if a.firmware == 'uefi':
    # A private pflash variable store prevents OVMF's NvVars FAT-file fallback
    # from writing to one of the disks whose contents the test protects.
    variables = out / 'OVMF_VARS.fd'
    shutil.copyfile(a.ovmf_vars, variables)
    args += ['-drive', f'if=pflash,unit=0,format=raw,readonly=on,file={a.ovmf}',
             '-drive', f'if=pflash,unit=1,format=raw,file={variables}']
cmd = [sys.executable, 'tools/serialtest.py', '--log', str(out / 'serial.log'),
       '--boot-timeout', '240', '--timeout', '240',
       '--cmd', 'disks ## SCSI #2', '--cmd', 'lsblk ## scsi2',
       '--cmd', 'cp /mnt/scsi/source.bin /mnt/scsi/copy.bin',
       '--cmd', 'selftest ## 0 failed', '--cmd', 'selftest posix ## 0 failed',
       '--', *args]
version = subprocess.check_output(['qemu-system-x86_64', '--version'], text=True).splitlines()[0]
record = {'qemu': version, 'command': cmd, 'before': before, 'source_sha256': digest(source),
          'scope': 'Live ISO discovery and FAT copy only; no installed-system durability claim.'}
(out / 'run.json').write_text(json.dumps(record, indent=2) + '\n')
run(cmd)
serial = (out / 'serial.log').read_text(encoding='utf-8')
for expected in ['target 0 lun 1:', 'target 1 lun 0:', 'target 2 lun 2:']:
    if expected not in serial:
        raise RuntimeError(f'missing discovery evidence: {expected}')
if 'controller failed closed' in serial:
    raise RuntimeError('controller failed during QEMU smoke test')
copy = out / 'copy.bin'
copy.unlink(missing_ok=True)
run(['mcopy', '-i', str(images[0]), '::copy.bin', str(copy)])
assert digest(copy) == digest(source), 'SCSI FAT copy hash mismatch'
after = [digest(image) for image in images]
assert before[1:] == after[1:], 'non-target disks changed'
record.update(after=after, copy_sha256=digest(copy), result='PASS')
(out / 'run.json').write_text(json.dumps(record, indent=2) + '\n')
print(f'PASS: SCSI topology, FAT copy SHA-256 and two unchanged non-target disks; {out}')
