#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Network regression: DHCP, ping and a SHA-256-verified 30 MiB download.
The multi model also unplugs/restores the preferred link through QMP.
These QEMU devices cannot validate physical Intel/ASIX/Realtek hardware.
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
MODELS = ['e1000', 'e1000e', 'igb', 'pcnet', 'rtl8139', 'virtio-net-pci', 'usb-ecm', 'multi']
BLOCK = bytes(range(256)) * 256
BLOCKS = 480  # 30 MiB
DIGEST = hashlib.sha256(BLOCK * BLOCKS).hexdigest()

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != '/probe.bin':
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Length', str(len(BLOCK) * BLOCKS))
        self.end_headers()
        for _ in range(BLOCKS):
            self.wfile.write(BLOCK)
    def log_message(self, *args):
        pass

def qmp_link(path, up):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(10)
        sock.connect(str(path))
        stream = sock.makefile('rwb', buffering=0)
        json.loads(stream.readline())
        for command in [
            {'execute': 'qmp_capabilities'},
            {'execute': 'set_link', 'arguments': {'name': 'nic0', 'up': up}},
        ]:
            stream.write(json.dumps(command).encode() + b'\n')
            while True:
                response = json.loads(stream.readline())
                if 'error' in response:
                    raise RuntimeError(response)
                if 'return' in response:
                    break

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--model', choices=MODELS, required=True)
    ap.add_argument('--iso', type=Path, default=ROOT / 'Banana_OS.iso')
    a = ap.parse_args()
    out = ROOT / 'build/logs' / ('network-' + a.model)
    out.mkdir(parents=True, exist_ok=True)
    log, qmp = out / 'serial.log', out / 'qmp.sock'
    log.unlink(missing_ok=True)
    qmp.unlink(missing_ok=True)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://10.0.2.2:{server.server_port}/probe.bin'
    base = ['-m', '512M', '-smp', '2', '-accel', 'tcg', '-cdrom', str(a.iso.resolve()),
            '-boot', 'd', '-vga', 'std']
    if a.model == 'usb-ecm':
        base += ['-nic', 'none', '-device', 'qemu-xhci,id=xhci', '-netdev', 'user,id=n',
                 '-device', 'usb-net,netdev=n']
    elif a.model == 'multi':
        base += ['-nic', 'none', '-netdev', 'user,id=n0',
                 '-device', 'e1000,netdev=n0,id=nic0,mac=52:54:00:12:34:56',
                 '-netdev', 'user,id=n1',
                 '-device', 'rtl8139,netdev=n1,id=nic1,mac=52:54:00:12:34:57',
                 '-qmp', f'unix:{qmp},server=on,wait=off']
    else:
        base += ['-nic', 'user,model=' + a.model]
    with a.iso.open('rb') as iso:
        iso_digest = hashlib.file_digest(iso, 'sha256').hexdigest()
    (out / 'provenance.json').write_text(json.dumps({
        'model': a.model, 'iso_sha256': iso_digest,
        'download_bytes': len(BLOCK) * BLOCKS, 'download_sha256': DIGEST,
        'qemu_args': base,
    }, indent=2) + '\n')
    commands = ['dhcp ## 10.0.2.15', 'ping -c 3 10.0.2.2 ## 0% packet loss',
                'ifconfig priority wifi ## Network priority: wifi',
                'ifconfig priority ethernet ## Network priority: ethernet',
                'ifconfig priority auto ## Network priority: auto']
    errors, done = [], threading.Event()
    def switches():
        try:
            for marker, up in [('__NET_DOWN__', False), ('__NET_UP__', True)]:
                while not done.wait(0.1):
                    if log.exists() and marker in log.read_text(errors='replace'):
                        qmp_link(qmp, up)
                        break
                else:
                    return
        except Exception as error:
            errors.append(str(error))
    if a.model == 'multi':
        threading.Thread(target=switches, daemon=True).start()
        commands += ['ifconfig eth0 up ## eth0 is now the active interface',
                     'echo __NET_DOWN__', '#wait 7',
                     'ifconfig priority ## active: eth1',
                     'dhcp ## 10.0.2.15',
                     f'curl -o /tmp/probe.bin {url}',
                     f'sha256sum /tmp/probe.bin ## {DIGEST}',
                     'rm /tmp/probe.bin',
                     'echo __NET_UP__', '#wait 7',
                     'ifconfig priority ## active: eth0']
    if a.model == 'multi':
        commands += ['selftest ipv6 ## 0 failed']
    commands += ['dhcp ## 10.0.2.15', f'curl -o /tmp/probe.bin {url}',
                 f'sha256sum /tmp/probe.bin ## {DIGEST}', 'rm /tmp/probe.bin']
    command_file = out / 'commands.txt'
    command_file.write_text('\n'.join(commands) + '\n')
    try:
        result = subprocess.run([sys.executable, 'tools/serialtest.py', '--timeout', '240',
                                 '--boot-timeout', '180', '--log', str(log),
                                 '--cmds-file', str(command_file), '--', *base], cwd=ROOT)
    finally:
        done.set()
        server.shutdown()
        qmp.unlink(missing_ok=True)
    if errors:
        raise RuntimeError('; '.join(errors))
    return result.returncode

if __name__ == '__main__':
    sys.exit(main())
