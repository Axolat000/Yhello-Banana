#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Prepare verified LLVM sources atomically; recover interrupted extractions."""
from pathlib import Path
import fcntl
import hashlib
import subprocess
import sys
import tempfile

VERSION = '18.1.8'
LLVM_SHA = 'f68cf90f369bc7d0158ba70d860b0cb34dbc163d6ff0ebc6cfa5e515b9b2e28d'
CMAKE_SHA = '59badef592dd34893cd319d42b323aaa990b452d05c7180ff20f23ab1b41e837'
LLVM_FILES = ('CMakeLists.txt', 'include/llvm/IR/Module.h',
              'lib/Target/X86/X86TargetMachine.cpp', 'utils/TableGen/CMakeLists.txt',
              'tools/llvm-config/CMakeLists.txt')
CMAKE_FILES = ('Modules/CMakePolicy.cmake', 'Modules/GNUInstallPackageDir.cmake')


def digest(path):
    with path.open('rb') as stream:
        checksum = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(block)
        return checksum.hexdigest()


def complete(path, files):
    return all((path / name).is_file() for name in files)


def preserve(path):
    backup = Path(tempfile.mkdtemp(prefix=path.name + '.previous-', dir=path.parent))
    backup.rmdir()
    path.rename(backup)
    print('Preserved previous source tree: ' + str(backup), flush=True)


def prepare(cache, name, checksum, files):
    directory = cache / f'{name}-{VERSION}.src'
    marker = directory / '.banana-source.sha256'
    if marker.is_file() and marker.read_text().strip() == checksum and complete(directory, files):
        return directory
    archive = cache / (directory.name + '.tar.xz')
    with tempfile.TemporaryDirectory(prefix='.prepare-' + name + '-', dir=cache) as tmp:
        staging = Path(tmp)
        if not archive.is_file() or digest(archive) != checksum:
            download = staging / archive.name
            url = f'https://github.com/llvm/llvm-project/releases/download/llvmorg-{VERSION}/{archive.name}'
            subprocess.run(['curl', '-fL', '--retry', '3', '-o', str(download), url], check=True)
            if digest(download) != checksum:
                raise RuntimeError('Checksum mismatch: ' + archive.name)
            download.replace(archive)
        print('Extracting verified ' + archive.name, flush=True)
        subprocess.run(['tar', '-C', str(staging), '-xJf', str(archive)], check=True)
        tree = staging / directory.name
        if not complete(tree, files):
            raise RuntimeError('Incomplete source archive: ' + archive.name)
        (tree / marker.name).write_text(checksum + '\n')
        if directory.exists() or directory.is_symlink():
            preserve(directory)
        tree.rename(directory)
    return directory


def main():
    cache = Path(__file__).resolve().parent / 'src'
    cache.mkdir(parents=True, exist_ok=True)
    source = Path(sys.argv[1]).absolute()
    # Separate invocations may share downloaded sources, never a half tree.
    with (cache / '.prepare.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if source == cache / f'llvm-{VERSION}.src':
            prepare(cache, 'llvm', LLVM_SHA, LLVM_FILES)
        elif not complete(source, LLVM_FILES):
            raise RuntimeError('LLVM_SRC is incomplete: ' + str(source) + '; use a complete tree or unset LLVM_SRC')
        companion = source.parent / 'cmake'
        if not complete(companion, CMAKE_FILES):
            if companion.parent != cache:
                raise RuntimeError('LLVM_SRC needs the matching CMake sources at ' + str(companion))
            cmake = prepare(cache, 'cmake', CMAKE_SHA, CMAKE_FILES)
            if companion.exists() or companion.is_symlink():
                preserve(companion)
            companion.symlink_to(cmake.name, target_is_directory=True)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit('LLVM source preparation failed: ' + str(error))
