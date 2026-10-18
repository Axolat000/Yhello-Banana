#!/bin/sh
# LLVM 18.1.8 for Mesa's optional CPU renderer. The native build provides
# llvm-config/tablegen; target objects use Banana's C library and ELF loader.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=$(realpath -m "${LLVM_BUILD:-$HERE/build}")
SRC=${LLVM_SRC:-$HERE/src/llvm-18.1.8.src}
JOBS=${JOBS:-$(nproc)}
mkdir -p "$HERE/src" "$OUT"
fetch() {
    name=$1 checksum=$2
    archive="$HERE/src/$name-18.1.8.src.tar.xz"
    [ -f "$archive" ] || curl -fL --retry 3 -o "$archive" "https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/$name-18.1.8.src.tar.xz"
    echo "$checksum  $archive" | sha256sum -c -
    tar -C "$HERE/src" -xJf "$archive"
}
if [ ! -d "$SRC" ]; then
    fetch llvm f68cf90f369bc7d0158ba70d860b0cb34dbc163d6ff0ebc6cfa5e515b9b2e28d
    fetch cmake 59badef592dd34893cd319d42b323aaa990b452d05c7180ff20f23ab1b41e837
    ln -sfn cmake-18.1.8.src "$HERE/src/cmake"
fi
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
ninja -C "$REF" -j"$JOBS" LLVM llvm-config
[ "${1:-native}" = native ] && exit 0
[ "$#" = 2 ] && [ "$1" = target ] || { echo 'usage: build.sh [native | target native-libOSMesa.so]' >&2; exit 1; }
python3 "$HERE/rebuild.py" "$REF" "$OUT/banana" "$2"
cp "$OUT/banana/libLLVM.so" "$OUT/libLLVM.so"
