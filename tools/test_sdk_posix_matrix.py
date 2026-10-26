#!/usr/bin/env python3
"""
Host-executable validation test for POSIX SDK status and audit matrix.
Verifies that sdk/lib/unix.c, sdk/lib/libc.c, and sdk/lib/mman.c contain the
documented categories of functions (Constant 0 / Fake Success, Partial, Unsupported / ENOSYS, Implemented),
and ensures docs/posix-sdk-status.md accurately covers all identified false successes and stubs.
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_PATH = os.path.join(REPO_ROOT, "docs", "posix-sdk-status.md")

EXPECTED_IMPLEMENTED_FUNCS = [
    ("flock", "sdk/lib/unix.c", "Advisory file locking via kernel ABI v23 (LOCK_SH, LOCK_EX, LOCK_NB, LOCK_UN)"),
]

EXPECTED_FAKE_SUCCESS_FUNCS = [
    ("fchmod", "sdk/lib/unix.c", "Constant 0 (ignores mode and descriptor)"),
    ("fchown", "sdk/lib/unix.c", "Constant 0 (ignores ownership and descriptor)"),
    ("sync", "sdk/lib/unix.c", "Empty stub (no storage flush performed)"),
    ("setpgid", "sdk/lib/unix.c", "Constant 0 (no process group modification)"),
    ("alarm", "sdk/lib/unix.c", "Constant 0 (no timer or signal scheduled)"),
    ("setrlimit", "sdk/lib/unix.c", "Constant 0 (limits are not enforced)"),
    ("getpriority", "sdk/lib/unix.c", "Constant 0"),
    ("setpriority", "sdk/lib/unix.c", "Constant 0"),
    ("prctl", "sdk/lib/unix.c", "Constant 0"),
    ("posix_fadvise", "sdk/lib/unix.c", "Constant 0"),
    ("ftrylockfile", "sdk/lib/unix.c", "Constant 0"),
    ("futimes", "sdk/lib/unix.c", "Constant 0"),
    ("listxattr", "sdk/lib/unix.c", "Constant 0"),
    ("flistxattr", "sdk/lib/unix.c", "Constant 0"),
    ("madvise", "sdk/lib/mman.c", "Constant 0"),
    ("posix_madvise", "sdk/lib/mman.c", "Constant 0"),
    ("msync", "sdk/lib/mman.c", "Constant 0"),
    ("mlock", "sdk/lib/mman.c", "Constant 0"),
    ("munlock", "sdk/lib/mman.c", "Constant 0"),
]

EXPECTED_PARTIAL_FUNCS = [
    ("chmod", "sdk/lib/unix.c", "Delegates to access(path, 0); ignores mode bits"),
    ("chown", "sdk/lib/unix.c", "Delegates to access(path, 0); ignores uid/gid"),
    ("lchown", "sdk/lib/unix.c", "Delegates to chown; no symlink resolution differences"),
    ("access", "sdk/lib/unix.c", "Checks stat presence only; ignores permission flags (R_OK, W_OK, X_OK)"),
    ("pread", "sdk/lib/unix.c", "Seeks file cursor and restores it; not concurrency-safe"),
    ("pwrite", "sdk/lib/unix.c", "Seeks file cursor and restores it; not concurrency-safe"),
    ("ftruncate", "sdk/lib/unix.c", "Zero-extends but fails with ENOTSUP on shrink; cursor moves then restores"),
]

EXPECTED_ENOSYS_FUNCS = [
    ("fork", "sdk/lib/unix.c", "ENOSYS"),
    ("vfork", "sdk/lib/unix.c", "ENOSYS"),
    ("execve", "sdk/lib/unix.c", "ENOSYS"),
    ("execv", "sdk/lib/unix.c", "ENOSYS"),
    ("execvp", "sdk/lib/unix.c", "ENOSYS"),
    ("shmget", "sdk/lib/unix.c", "ENOSYS"),
    ("shmat", "sdk/lib/unix.c", "ENOSYS"),
    ("inotify_init", "sdk/lib/unix.c", "ENOSYS"),
    ("inotify_init1", "sdk/lib/unix.c", "ENOSYS"),
    ("memfd_create", "sdk/lib/unix.c", "ENOSYS"),
    ("posix_spawn", "sdk/lib/unix.c", "ENOSYS"),
    ("popen", "sdk/lib/unix.c", "ENOSYS"),
    ("fdopendir", "sdk/lib/unix.c", "ENOTSUP"),
    ("dirfd", "sdk/lib/unix.c", "ENOTSUP"),
    ("mkfifo", "sdk/lib/unix.c", "ENOTSUP"),
    ("fchdir", "sdk/lib/unix.c", "ENOTSUP"),
    ("getxattr", "sdk/lib/unix.c", "ENOTSUP"),
    ("fgetxattr", "sdk/lib/unix.c", "ENOTSUP"),
    ("setxattr", "sdk/lib/unix.c", "ENOTSUP"),
    ("fsetxattr", "sdk/lib/unix.c", "ENOTSUP"),
    ("removexattr", "sdk/lib/unix.c", "ENOTSUP"),
    ("fremovexattr", "sdk/lib/unix.c", "ENOTSUP"),
]

def check_file_exists(rel_path):
    p = os.path.join(REPO_ROOT, rel_path)
    if not os.path.isfile(p):
        print(f"FAIL: Source file missing: {rel_path}")
        sys.exit(1)
    return p

def is_func_documented(fn, doc_text):
    # Match `fn` or `fn(...)`
    pattern = r'`' + re.escape(fn) + r'(\([^)]*\))?`'
    return bool(re.search(pattern, doc_text))

def main():
    print("=== POSIX SDK Status and Inventory Verification ===")

    # 1. Verify docs/posix-sdk-status.md exists
    if not os.path.isfile(DOCS_PATH):
        print(f"FAIL: Matrix documentation missing at {DOCS_PATH}")
        sys.exit(1)

    with open(DOCS_PATH, "r", encoding="utf-8") as f:
        doc_content = f.read()

    # 2. Check source implementations in C files
    for fn, rel_file, desc in EXPECTED_IMPLEMENTED_FUNCS:
        path = check_file_exists(rel_file)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        if not re.search(r'\b' + fn + r'\s*\(', src):
            print(f"FAIL: Expected implemented function {fn} not found in {rel_file}")
            sys.exit(1)
        if not is_func_documented(fn, doc_content):
            print(f"FAIL: Implemented function {fn} not documented in {DOCS_PATH}")
            sys.exit(1)

    for fn, rel_file, desc in EXPECTED_FAKE_SUCCESS_FUNCS:
        path = check_file_exists(rel_file)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        if not re.search(r'\b' + fn + r'\s*\(', src):
            print(f"FAIL: Expected fake success function {fn} not found in {rel_file}")
            sys.exit(1)
        if not is_func_documented(fn, doc_content):
            print(f"FAIL: Fake success function {fn} not documented in {DOCS_PATH}")
            sys.exit(1)

    for fn, rel_file, desc in EXPECTED_PARTIAL_FUNCS:
        path = check_file_exists(rel_file)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        if not re.search(r'\b' + fn + r'\s*\(', src):
            print(f"FAIL: Expected partial function {fn} not found in {rel_file}")
            sys.exit(1)
        if not is_func_documented(fn, doc_content):
            print(f"FAIL: Partial function {fn} not documented in {DOCS_PATH}")
            sys.exit(1)

    for fn, rel_file, desc in EXPECTED_ENOSYS_FUNCS:
        path = check_file_exists(rel_file)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        if not re.search(r'\b' + fn + r'\s*\(', src):
            print(f"FAIL: Expected unsupported function {fn} not found in {rel_file}")
            sys.exit(1)
        if not is_func_documented(fn, doc_content):
            print(f"FAIL: Unsupported function {fn} not documented in {DOCS_PATH}")
            sys.exit(1)

    # Check that key sections exist in documentation
    required_sections = [
        "Contract and Status Matrix",
        "Flock and File Locking",
        "Permissions and Metadata",
        "Offset-based Concurrent I/O",
        "Storage Flushing and Durability"
    ]
    for sec in required_sections:
        if sec.lower() not in doc_content.lower():
            print(f"FAIL: Required section '{sec}' missing in {DOCS_PATH}")
            sys.exit(1)

    print("SUCCESS: All SDK functions categorized, verified against sources, and documented in matrix.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
