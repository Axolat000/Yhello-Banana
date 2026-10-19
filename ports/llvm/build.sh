#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# LLVM 18.1.8 for Mesa's optional CPU renderer. The native build provides
# llvm-config/tablegen; target objects use Banana's C library and ELF loader.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=$(realpath -m "${LLVM_BUILD:-$HERE/build}")
SRC=${LLVM_SRC:-$HERE/src/llvm-18.1.8.src}
JOBS=${JOBS:-$(nproc)}
mkdir -p "$HERE/src" "$OUT"
MODE=${1:-native}
case "$MODE" in
    native|configure) [ "$#" -le 1 ] ;;
    target) [ "$#" -eq 2 ] ;;
    *) echo 'usage: build.sh [configure | native | target native-libOSMesa.so]' >&2; exit 1 ;;
esac
python3 "$HERE/prepare_source.py" "$SRC"
REF=${LLVM_REF:-$OUT/ref}
[ -f "$REF/build.ninja" ] || cmake -G Ninja -S "$SRC" -B "$REF" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_FFI=OFF -DLLVM_ENABLE_TERMINFO=OFF \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_BUILD_TOOLS=OFF -DLLVM_ENABLE_EH=OFF -DLLVM_ENABLE_RTTI=OFF \
    -DLLVM_APPEND_VC_REV=OFF -DLLVM_FORCE_VC_REVISION=llvmorg-18.1.8 \
    -DLLVM_FORCE_VC_REPOSITORY=https://github.com/llvm/llvm-project \
    '-DCMAKE_CXX_FLAGS=-include cstdint'
[ "$MODE" != configure ] || exit 0
ninja -C "$REF" -j"$JOBS" LLVM llvm-config
[ "${1:-native}" = native ] && exit 0
[ "$#" = 2 ] && [ "$1" = target ] || { echo 'usage: build.sh [native | target native-libOSMesa.so]' >&2; exit 1; }
python3 "$HERE/rebuild.py" "$REF" "$OUT/banana" "$2"
cp "$OUT/banana/libLLVM.so" "$OUT/libLLVM.so"
