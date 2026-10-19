#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reads an exFAT image on its own (not through Banana OS's driver), to
check what Banana OS wrote: the tree, and file contents.

usage: exfat_check.py IMAGE list              every file and folder, with sizes
       exfat_check.py IMAGE cat PATH          a file's contents
       exfat_check.py IMAGE cmp PATH LOCAL    exit 0 if the file equals LOCAL
"""
import struct, sys

class Vol:
    def __init__(self, path):
        self.f = open(path, "rb")
        b = self.read(0, 512)
        if b[3:11] != b"EXFAT   ":
            sys.exit("not an exFAT volume")
        (self.fat_off, self.fat_len, self.heap_off, self.nclus, self.root) = struct.unpack_from("<IIIII", b, 80)
        self.bps = 1 << b[108]
        self.cbytes = self.bps << b[109]

    def read(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def fat(self, c):
        return struct.unpack("<I", self.read(self.fat_off * self.bps + c * 4, 4))[0]

    def clusters(self, first, nofat, size=None):
        n = None if size is None else (size + self.cbytes - 1) // self.cbytes
        c, k = first, 0
        while 2 <= c <= self.nclus + 1 and (n is None or k < n):
            yield c
            k += 1
            c = c + 1 if nofat else self.fat(c)

    def data(self, first, nofat, size):
        out = bytearray()
        for c in self.clusters(first, nofat, size):
            out += self.read((self.heap_off + (c - 2) * (self.cbytes // self.bps)) * self.bps, self.cbytes)
        return bytes(out[:size])

    def entries(self, first, nofat, size):
        """(name, is_dir, first, nofat, size, valid) of a folder"""
        d = self.data(first, nofat, size) if size is not None else b"".join(
            self.read((self.heap_off + (c - 2) * (self.cbytes // self.bps)) * self.bps, self.cbytes)
            for c in self.clusters(first, False))
        p = 0
        while p + 32 <= len(d):
            t = d[p]
            if t == 0:
                break
            if t == 0x85:
                n = d[p + 1]
                s = d[p:p + (n + 1) * 32]
                csum = 0
                for i, byte in enumerate(s):
                    if i in (2, 3):
                        continue
                    csum = (((csum & 1) << 15) | (csum >> 1)) + byte & 0xFFFF
                if csum != struct.unpack_from("<H", s, 2)[0]:
                    raise SystemExit(f"bad entry set checksum at {p}")
                attr = struct.unpack_from("<H", s, 4)[0]
                st = s[32:64]
                flags, namelen = st[1], st[3]
                valid, = struct.unpack_from("<Q", st, 8)
                first_c, size_c = struct.unpack_from("<IQ", st, 20)
                name = b"".join(s[64 + k * 32 + 2:64 + k * 32 + 32] for k in range(n - 1)).decode("utf-16-le")[:namelen]
                yield name, bool(attr & 0x10), first_c, bool(flags & 2), size_c, valid
                p += (n + 1) * 32
            else:
                p += 32

    def walk(self, first=None, nofat=False, size=None, prefix=""):
        for name, is_dir, fc, nf, sz, valid in self.entries(first or self.root, nofat, size):
            path = prefix + "/" + name
            yield path, is_dir, fc, nf, sz, valid
            if is_dir:
                yield from self.walk(fc, nf, sz, path)

    def file(self, path):
        for p, is_dir, fc, nf, sz, valid in self.walk():
            if p == path and not is_dir:
                return self.data(fc, nf, valid) + bytes(sz - valid)
        sys.exit(f"no such file: {path}")

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    v = Vol(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == "list":
        for p, is_dir, fc, nf, sz, valid in v.walk():
            print(f"{p}{'/' if is_dir else ''}  {sz}" + ("" if is_dir else f"  {'run' if nf else 'chain'}"))
    elif cmd == "cat":
        sys.stdout.buffer.write(v.file(sys.argv[3]))
    elif cmd == "cmp":
        same = v.file(sys.argv[3]) == open(sys.argv[4], "rb").read()
        print("same" if same else "DIFFERENT")
        sys.exit(0 if same else 1)
    else:
        sys.exit(__doc__)

if __name__ == "__main__":
    main()
