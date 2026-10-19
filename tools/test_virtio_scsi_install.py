#!/usr/bin/env python3
"""Install and restart a disposable SCSI VM; check sync persistence and e2fsck.

Install and data-copy phases are killed after sync; later phases require clean
guest shutdown. This does not test journal-boundary faults or application fsync.
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
p.add_argument('--firmware', choices=['bios', 'uefi'], default='bios')
p.add_argument('--transport', choices=['legacy', 'modern'], default='modern')
p.add_argument('--ovmf', default='/usr/share/OVMF/OVMF_CODE_4M.fd')
p.add_argument('--ovmf-vars', default='/usr/share/OVMF/OVMF_VARS_4M.fd')
a = p.parse_args()
out = root / 'build/test/virtio-scsi' / f'install-{a.transport}-{a.firmware}'
out.mkdir(parents=True, exist_ok=True)

def run(cmd):
    subprocess.run(cmd, cwd=root, check=True)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()

payload = out / 'source.bin'
payload.write_bytes(bytes((i * 17 + i // 512) % 251 for i in range(519 * 512 + 73)))
images = [out / f'disk{i}.img' for i in range(3)]
for i, image in enumerate(images):
    with image.open('wb') as f:
        f.truncate((1024 if i == 0 else 128) * 1024 * 1024)
    if i:
        run(['mformat', '-i', str(image), '-F', '::'])
        run(['mcopy', '-i', str(image), str(payload), '::source.bin'])
before = [digest(image) for image in images[1:]]
transport = 'disable-modern=on' if a.transport == 'legacy' else 'disable-legacy=on'
args = ['-accel', 'tcg', '-m', '1024', '-smp', '4', '-nic', 'user,model=e1000',
        '-device', f'virtio-scsi-pci,id=sc0,{transport}']
for i, image in enumerate(images):
    args += ['-drive', f'file={image},format=raw,if=none,id=d{i},cache=writeback',
             '-device', f'scsi-hd,drive=d{i},bus=sc0.0,scsi-id={i},lun=1' +
             (',bootindex=2' if i == 0 else '')]
if a.firmware == 'uefi':
    # A private pflash variable store prevents OVMF's NvVars FAT-file fallback
    # from writing to one of the disks whose contents the test protects.
    variables = out / 'OVMF_VARS.fd'
    shutil.copyfile(a.ovmf_vars, variables)
    args += ['-drive', f'if=pflash,unit=0,format=raw,readonly=on,file={a.ovmf}',
             '-drive', f'if=pflash,unit=1,format=raw,file={variables}']
record = {'qemu': subprocess.check_output(['qemu-system-x86_64', '--version'], text=True).splitlines()[0],
          'before': before, 'source_sha256': digest(payload), 'commands': [],
          'scope': 'Install, no-CD boot, post-sync VM kill/restart, clean shutdown/restart, system exclusion and same-ISO update comparison.'}

def phase(name, commands, cd, poweroff=False):
    cmd = [sys.executable, 'tools/serialtest.py', '--log', str(out / f'{name}.log'),
           '--boot-timeout', '240', '--timeout', '240']
    if poweroff:
        cmd += ['--poweroff']
    for c in commands:
        cmd += ['--cmd', c]
    cmd += ['--', *args, '-boot', 'd' if cd else 'c']
    if cd:
        cmd += ['-drive', f"file={root / 'Banana_OS.iso'},if=none,id=cdrom,media=cdrom,format=raw",
                '-device', 'ide-cd,drive=cdrom,bootindex=1']
    record['commands'].append(cmd)
    (out / 'run.json').write_text(json.dumps(record, indent=2) + '\n')
    run(cmd)
    text = (out / f'{name}.log').read_text()
    assert 'controller failed closed' not in text
    return text

phase('install', ['echo scsi-persistent-proof > /home/banana/scsi-proof.txt',
                  'install 1 --minimal\ry ## install: done.',
                  'sync ## filesystem written to disk'], True)
boot = phase('boot', ['cat /home/banana/scsi-proof.txt ## scsi-persistent-proof',
                      'lsblk ## (Banana OS)',
                      'cp /mnt/scsi/source.bin /home/banana/scsi-copy.bin',
                      'sync ## filesystem written to disk'], False)
# The system block device is marked instead of automounted, while data mounts.
assert 'scsi0' in boot and 'mounted at /mnt/scsi' in boot
assert 'fat32: scsi0' not in boot
phase('restart', ['cat /home/banana/scsi-proof.txt ## scsi-persistent-proof',
                  'fsck ## ext4', 'sync ## filesystem written to disk'], False, poweroff=True)
phase('clean-restart', ['cat /home/banana/scsi-proof.txt ## scsi-persistent-proof',
                        'lsblk ## (Banana OS)'], False, poweroff=True)
phase('update-comparison', ['update 1 ## already the same as this CD'], True)
after = [digest(image) for image in images[1:]]
assert before == after, 'non-target disk contents changed'
# Existing fsdisk layout: ext4 starts at 128 MiB, ending before the 1 MiB tail.
fs_image = out / 'ext4.img'
with images[0].open('rb') as src, fs_image.open('wb') as dst:
    src.seek(128 * 1024 * 1024)
    left = images[0].stat().st_size - 129 * 1024 * 1024
    while left:
        data = src.read(min(left, 1024 * 1024))
        assert data
        dst.write(data)
        left -= len(data)
run([shutil.which('e2fsck') or '/sbin/e2fsck', '-fn', str(fs_image)])
copy = out / 'copy.bin'
copy.unlink(missing_ok=True)
run([shutil.which('debugfs') or '/sbin/debugfs', '-R',
     f'dump /home/banana/scsi-copy.bin {copy}', str(fs_image)])
assert digest(copy) == digest(payload), 'installed file hash mismatch'
record.update(after=after, copy_sha256=digest(copy), result='PASS')
(out / 'run.json').write_text(json.dumps(record, indent=2) + '\n')
print(f'PASS: SCSI install/restart, synced file hash, exclusion, non-target hashes and e2fsck; {out}')
