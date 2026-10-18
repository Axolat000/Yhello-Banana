#!/usr/bin/env python3
"""The App Store's facts about the packages built into the kernel, made at
build time (kernel/builtin_apps.c: builtin_meta) - so the boot does not
unpack and hash every one of them again to list them (~0.9 s in QEMU).

For each package: its name as the App Store lists it, the length of its
copy in the kernel (to tell a stale table), the unpacked size and SHA-256,
and a skeleton of it - the same file table, the manifest, every other
file empty - for the kernel's own pkg_inspect_mem() to read.

usage: mkbuiltinmeta.py OUT NAME=FILE ...   (FILE: .bpk, or .bpk.z: zlib)

Format (little endian): "BMT1", uint32 count, then per package:
char name[40]; uint32 blob_len; uint32 size; uint8 sha256[32];
uint32 skel_len; skel_len bytes, padded to 4.
"""
import hashlib
import struct
import sys
import zlib

ENT = struct.Struct("<56sII")


def skeleton(bpk, where):
    if bpk[:4] != b"BPK1":
        sys.exit(f"mkbuiltinmeta: {where}: not a .bpk package")
    (count,) = struct.unpack_from("<I", bpk, 4)
    table = 8 + count * ENT.size
    entries, manifest = [], b""
    for i in range(count):
        raw, off, size = ENT.unpack_from(bpk, 8 + i * ENT.size)
        name = raw.split(b"\0", 1)[0]
        if name == b"manifest":
            manifest = bpk[off:off + size]
        entries.append((raw, name))
    out = bytearray(b"BPK1" + struct.pack("<I", count))
    data_at = table
    for raw, name in entries:
        if name == b"manifest":
            out += ENT.pack(raw, data_at, len(manifest))
        else:
            out += ENT.pack(raw, data_at + len(manifest), 0)     # (there, but empty)
    out += manifest
    return bytes(out)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = bytearray(b"BMT1" + struct.pack("<I", len(sys.argv) - 2))
    for arg in sys.argv[2:]:
        name, path = arg.split("=", 1)
        blob = open(path, "rb").read()
        bpk = zlib.decompress(blob) if path.endswith(".z") else blob
        skel = skeleton(bpk, path)
        if len(name.encode()) >= 40:
            sys.exit(f"mkbuiltinmeta: {name}: name too long")
        out += struct.pack("<40sII", name.encode(), len(blob), len(bpk))
        out += hashlib.sha256(bpk).digest()
        out += struct.pack("<I", len(skel)) + skel + b"\0" * (-len(skel) % 4)
    open(sys.argv[1], "wb").write(out)


main()
