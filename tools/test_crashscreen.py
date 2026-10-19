#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real formatter/renderer; export screenshots without booting.
Build/run the ISO yourself, then use crashtest for the in-OS preview.
"""
from pathlib import Path
import ctypes as C
import os
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/test/crashscreen'
OUT.mkdir(parents=True, exist_ok=True)

def png(path, width, height, rows):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    content = b'\x89PNG\r\n\x1a\n'
    content += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    content += chunk(b'IDAT', zlib.compress(b''.join(b'\0' + row for row in rows)))
    path.write_bytes(content + chunk(b'IEND', b''))

with tempfile.TemporaryDirectory(prefix='banana-crashscreen-') as tmp:
    library = Path(tmp) / 'screen.so'
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=gnu11', '-O2', '-shared', '-fPIC',
                    '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'kernel'),
                    str(ROOT / 'tools/tests/crashscreen.c'), str(ROOT / 'kernel/crash_draw.c'),
                    str(ROOT / 'kernel/kstring.c'), str(ROOT / 'kernel/font8x8.c'),
                    '-o', str(library)], check=True)
    lib = C.CDLL(str(library))
    lib.crash_test_format.argtypes = [C.c_void_p, C.c_size_t, C.c_int, C.c_int]
    lib.crash_test_render.argtypes = [C.c_void_p] + [C.c_int] * 5
    for bits in (32, 64):
        reports = []
        for kind in range(8):
            buf = C.create_string_buffer(8192)
            rows = lib.crash_test_format(buf, len(buf), bits, kind)
            report = buf.value.decode('ascii')
            reports.append(report)
            assert rows < 40 and len(report) < 6000
            assert '0x123456789ABCDEF0' in report if bits == 64 else '0x9ABCDEF0' in report
            assert 'REGISTERS' in report if kind != 2 else 'general registers unavailable' in report
            assert '%.' not in report
        assert 'page not present / write / supervisor' in reports[0]
        assert 'Selector index: 5   Table: IDT   External: yes' in reports[1]
        assert 'KERNEL_CHECK_FAILURE' in reports[2]
        assert 'Unavailable: SP is outside' in reports[5]
        for flag in ('protection violation', 'instruction fetch', 'user', 'reserved-bit', 'protection key', 'shadow stack'):
            assert flag in reports[6]
        (OUT / f'report-{bits}.txt').write_text(reports[0])
    sentinel = 0xDEADBEEF
    for w, h in ((1, 1), (80, 25), (320, 200), (640, 480), (800, 600), (1024, 768), (1920, 1080)):
        stride = w + 8
        size = stride * h
        buf = (C.c_uint32 * (size + 32))(*([sentinel] * (size + 32)))
        ptr = C.byref(buf, 16 * 4)
        lib.crash_test_render(ptr, w, h, stride * 4, 64, 0)
        assert all(buf[i] == sentinel for i in range(16))
        assert all(buf[16 + size + i] == sentinel for i in range(16))
        for y in range(h):
            assert all(buf[16 + y * stride + x] == sentinel for x in range(w, stride))
        if w >= 640:
            rows = []
            for y in range(h):
                row = bytearray()
                for x in range(w):
                    pixel = buf[16 + y * stride + x]
                    row.extend(((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255))
                rows.append(bytes(row))
            png(OUT / f'preview-{w}x{h}.png', w, h, rows)
    invalid = (C.c_uint32 * 256)(*([sentinel] * 256))
    lib.crash_test_render(invalid, 16, 16, 12, 64, 0)
    assert all(pixel == sentinel for pixel in invalid)
    lib.crash_test_render(invalid, 0, 0, 0, 64, 0)
    assert all(pixel == sentinel for pixel in invalid)
    print('PASS: 32/64-bit reports, fault flags, long messages and clipped/padded framebuffers')
    print(f'Preview images and reports: {OUT}')
