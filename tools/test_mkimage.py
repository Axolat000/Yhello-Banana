#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise real FAT + ISO image generation, extraction and corruption rejection."""
import contextlib
import io
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from mkimage import compact_kernels, fat_file, iso_find

TOOLS = ("mformat", "mmd", "mcopy", "xorriso")
HERE = Path(__file__).resolve().parent


@unittest.skipUnless(all(shutil.which(x) for x in TOOLS), "requires mtools and xorriso")
class SharedKernelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        tree = cls.root / "iso"
        (tree / "boot").mkdir(parents=True)
        cls.files = {"boot/bios.bin": bytes(2048), "boot/kernel.bin": bytes(range(256)) * 8193,
                     "boot/kernel64.bin": bytes(reversed(range(256))) * 12289,
                     "medium.cfg": b"preserve trailing files too"}
        for name, data in cls.files.items():
            (tree / name).write_bytes(data)
        esp = tree / "efi.img"
        with esp.open("wb") as f:
            f.truncate(32 * 1024 * 1024)
        cls.run_tool(["python3", str(HERE / "format_efi.py"), str(esp)])
        cls.run_tool(["mmd", "-i", str(esp), "::/boot"])
        for name in ("kernel.bin", "kernel64.bin"):
            cls.run_tool(["mcopy", "-i", str(esp), str(tree / "boot" / name), "::/boot/"])
        image = cls.root / "raw.iso"
        cls.run_tool(["xorriso", "-as", "mkisofs", "-quiet", "-R", "-J",
                      "-b", "boot/bios.bin", "-no-emul-boot", "-boot-load-size", "4",
                      "-eltorito-alt-boot", "-e", "efi.img", "-no-emul-boot",
                      "-o", str(image), str(tree)])
        cls.raw = image.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            cls.shared = compact_kernels(bytearray(cls.raw))

    @classmethod
    def run_tool(cls, args):
        subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_primary_files_and_esp_are_preserved(self):
        for name, expected in self.files.items():
            lba, size = iso_find(self.shared, name)
            self.assertEqual(self.shared[lba * 2048:lba * 2048 + size], expected)
        old, size = iso_find(self.raw, "efi.img")
        new, newsize = iso_find(self.shared, "efi.img")
        self.assertEqual(size, newsize)
        self.assertEqual(self.raw[old*2048:old*2048+size],
                         self.shared[new*2048:new*2048+size])
        for name in ("kernel.bin", "kernel64.bin"):
            lba, length = iso_find(self.shared, "boot/" + name)
            off, fatlength = fat_file(self.shared, new*2048, size, "boot/" + name)
            self.assertEqual((lba*2048, length), (off, fatlength))

    def test_volume_sizes_and_saved_space(self):
        saved = sum(((len(v)+2047)//2048)*2048 for k,v in self.files.items() if k.startswith("boot/kernel"))
        self.assertEqual(len(self.raw)-len(self.shared), saved)
        seen = 0
        for block in range(16, 32):
            pos = block * 2048
            if self.shared[pos] == 255:
                break
            if self.shared[pos] not in (1, 2):
                continue
            seen += 1
            self.assertEqual(struct.unpack_from("<I", self.shared, pos+80)[0]*2048, len(self.shared))
            self.assertEqual(struct.unpack_from(">I", self.shared, pos+84)[0]*2048, len(self.shared))

        self.assertEqual(seen, 2)

    def test_rock_ridge_and_joliet_extraction(self):
        image = self.root / "shared.iso"
        image.write_bytes(self.shared)
        for options in ([], ["-read_fs", "norock"]):
            dest = self.root / ("joliet" if options else "rr")
            self.run_tool(["xorriso"] + options + ["-osirrox", "on", "-indev", str(image),
                           "-extract", "/", str(dest)])
            for name, expected in self.files.items():
                self.assertEqual((dest / name).read_bytes(), expected)

    def test_mismatched_kernel_is_rejected(self):
        bad = bytearray(self.raw)
        lba, _ = iso_find(bad, "boot/kernel.bin")
        bad[lba*2048+123] ^= 1
        with self.assertRaisesRegex(ValueError, "mismatch"):
            compact_kernels(bad)

    def test_corrupt_cluster_chain_is_rejected(self):
        bad = bytearray(self.raw)
        lba, size = iso_find(bad, "efi.img")
        base = lba*2048
        off, _ = fat_file(bad, base, size, "boot/kernel.bin")
        boot = bad[base:base+512]
        reserved = struct.unpack_from("<H", boot, 14)[0]
        fatlen = struct.unpack_from("<H", boot, 22)[0]
        roots = struct.unpack_from("<H", boot, 17)[0]
        data = reserved + boot[16]*fatlen + (roots*32+511)//512
        first = (off-base-data*512)//(boot[13]*512)+2
        struct.pack_into("<H", bad, base+reserved*512+first*2, first)
        with self.assertRaisesRegex(ValueError, "cluster chain"):
            compact_kernels(bad)


if __name__ == "__main__":
    unittest.main()
