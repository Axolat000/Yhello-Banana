#!/usr/bin/env python3
"""Independently decode the real encoder output using Python's zlib."""
from pathlib import Path
import struct, zlib
p=Path(__file__).resolve().parents[1]/'build/test/screenshot/capture.png'
b=p.read_bytes()
assert b[:8]==b'\x89PNG\r\n\x1a\n'
i=8
compressed=b''
types=[]
while i<len(b):
    n=struct.unpack_from('>I',b,i)[0]
    kind=b[i+4:i+8]; data=b[i+8:i+8+n]
    assert zlib.crc32(kind+data)==struct.unpack_from('>I',b,i+8+n)[0]
    types.append(kind)
    if kind==b'IHDR': assert struct.unpack('>IIBBBBB',data)==(2,2,8,2,0,0,0)
    if kind==b'IDAT': compressed+=data
    i+=n+12
assert types==[b'IHDR',b'IDAT',b'IEND'] and i==len(b)
assert zlib.decompress(compressed)==bytes([0,255,0,0,0,255,0,0,0,0,255,255,255,255])
print('PNG decoded: dimensions, CRCs, stride, RGB channels and pixels correct')
