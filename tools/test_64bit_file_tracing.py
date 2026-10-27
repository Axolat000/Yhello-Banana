#!/usr/bin/env python3
"""
tools/test_64bit_file_tracing.py

Host test and static audit harness for base-19 step 1:
Tracing file sizes and offsets across Banana OS:
SDK (libc/posix/stdio) -> Banana API -> Kernel VFS (kernel/fs.c, fs.h) ->
Installed Storage / Backend (kernel/fsdisk.c) -> ext4 (kernel/ext4.c, ext4.h).

Validates:
1. Presence of identified 32-bit truncation bottlenecks across layers.
2. Structure member definitions and sizes (e.g. banana_stat_t, fs_file_t, ext4_stat_t).
3. System call registration spec in kernel/app.c (e.g. seek uses "iii" expecting 32-bit registers on 32-bit syscall dispatch).
4. FS_MAX_FILE_SIZE definition in kernel/fs.h.
5. Inode 64-bit extent and size compatibility in kernel/ext4.c.
"""

import os
import re
import sys
import unittest

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

class Test64BitFileTracing(unittest.TestCase):
    def read_file(self, relpath):
        path = os.path.join(ROOT_DIR, relpath)
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()

    def test_fs_max_file_size_defined(self):
        """kernel/fs.h defines FS_MAX_FILE_SIZE as 0x7FFFF000u (approx 2 GiB limit)"""
        content = self.read_file("kernel/fs.h")
        m = re.search(r"#define\s+FS_MAX_FILE_SIZE\s+([^\n]+)", content)
        self.assertIsNotNone(m, "FS_MAX_FILE_SIZE must be defined in kernel/fs.h")
        self.assertIn("0x7FFFF000u", m.group(1))

    def test_fs_file_t_fields(self):
        """kernel/fs.h fs_file_t defines size and cap as uint32_t"""
        content = self.read_file("kernel/fs.h")
        # Match size field in fs_file_t
        m_size = re.search(r"typedef\s+struct\s*\{[^}]*uint32_t\s+size;[^}]*\}\s*fs_file_t;", content, re.DOTALL)
        self.assertIsNotNone(m_size, "fs_file_t.size is currently uint32_t")

        m_cap = re.search(r"typedef\s+struct\s*\{[^}]*uint32_t\s+cap;[^}]*\}\s*fs_file_t;", content, re.DOTALL)
        self.assertIsNotNone(m_cap, "fs_file_t.cap is currently uint32_t")

    def test_fs_stat_t_fields(self):
        """kernel/fs.h fs_stat_t defines size as uint32_t"""
        content = self.read_file("kernel/fs.h")
        m = re.search(r"typedef\s+struct\s*\{[^}]*uint32_t\s+size;[^}]*\}\s*fs_stat_t;", content, re.DOTALL)
        self.assertIsNotNone(m, "fs_stat_t.size is currently uint32_t")

    def test_banana_api_stat_fields(self):
        """sdk/include/banana_api.h defines banana_stat_t.size as unsigned int (32 bits)"""
        content = self.read_file("sdk/include/banana_api.h")
        m = re.search(r"typedef\s+struct\s*\{[^}]*unsigned\s+int\s+size;[^}]*\}\s*banana_stat_t;", content, re.DOTALL)
        self.assertIsNotNone(m, "banana_stat_t.size is unsigned int (32-bit)")

    def test_banana_api_seek_signature(self):
        """sdk/include/banana_api.h seek signature uses 'long off' (32 bits on i686)"""
        content = self.read_file("sdk/include/banana_api.h")
        m = re.search(r"long\s+\(\*seek\)\(int\s+fd,\s+long\s+off,\s+int\s+whence\);", content)
        self.assertIsNotNone(m, "banana_api_t.seek signature uses long off and returns long")

    def test_ext4_stat_has_64bit_size(self):
        """kernel/ext4.h ext4_stat_t already uses uint64_t for size and blocks"""
        content = self.read_file("kernel/ext4.h")
        m = re.search(r"typedef\s+struct\s*\{[^}]*uint64_t\s+size;[^}]*uint64_t\s+blocks;[^}]*\}\s*ext4_stat_t;", content, re.DOTALL)
        self.assertIsNotNone(m, "ext4_stat_t already defines uint64_t size and blocks")

    def test_ext4_ops_have_64bit_offsets(self):
        """kernel/ext4.h ext4_read/write/truncate already take uint64_t off/size"""
        content = self.read_file("kernel/ext4.h")
        self.assertRegex(content, r"long\s+ext4_read\(uint32_t\s+ino,\s+uint64_t\s+off,\s+void\*\s+buf,\s+uint32_t\s+len\);")
        self.assertRegex(content, r"int\s+ext4_write\(uint32_t\s+ino,\s+uint64_t\s+off,\s+const\s+void\*\s+buf,\s+uint32_t\s+len\);")
        self.assertRegex(content, r"int\s+ext4_truncate\(uint32_t\s+ino,\s+uint64_t\s+size\);")

    def test_fs_mount_ops_are_32bit(self):
        """kernel/fs.h fs_mount_ops_t still uses uint32_t off and size"""
        content = self.read_file("kernel/fs.h")
        self.assertRegex(content, r"int\s+\(\*read_at\)\(void\*\s+ctx,\s+uint32_t\s+node,\s+uint32_t\s+off,\s+void\*\s+buf,\s+uint32_t\s+len\);")
        self.assertRegex(content, r"int\s+\(\*write_at\)\(void\*\s+ctx,\s+uint32_t\s+node,\s+uint32_t\s+off,\s+const\s+void\*\s+data,\s+uint32_t\s+len\);")
        self.assertRegex(content, r"int\s+\(\*truncate\)\(void\*\s+ctx,\s+uint32_t\s+node,\s+uint32_t\s+size\);")

    def test_app_afd_t_pos_is_32bit(self):
        """kernel/app.c afd_t.pos is uint32_t"""
        content = self.read_file("kernel/app.c")
        m = re.search(r"typedef\s+struct\s*\{[^}]*uint32_t\s+pos;[^}]*\}\s*afd_t;", content, re.DOTALL)
        self.assertIsNotNone(m, "kernel/app.c afd_t.pos is uint32_t")

    def test_app_syscall_spec_seek(self):
        """kernel/app.c registers seek as 'iii' in g_spec"""
        content = self.read_file("kernel/app.c")
        self.assertIn('SPEC(seek, "iii", 0);', content)

    def test_stdio_fseek_uses_long(self):
        """sdk/lib/stdio.c fseek and ftell use 'long'"""
        content = self.read_file("sdk/lib/stdio.c")
        self.assertRegex(content, r"int\s+fseek\(FILE\*\s+f,\s+long\s+off,\s+int\s+whence\)")
        self.assertRegex(content, r"long\s+ftell\(FILE\*\s+f\)")


if __name__ == "__main__":
    unittest.main()
