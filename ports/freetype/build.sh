#!/bin/sh
# FreeType 2.13.3 port for Banana OS.
# Builds libfreetype.a statically for Banana OS applications (SDK) and characterization tests.
# Usage: ports/freetype/build.sh [i686|x86_64] [output dir]
set -e

ARCH=${1:-x86_64}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SDK=$ROOT/sdk
OUT=$(realpath -m "${2:-$HERE/build/$ARCH}")

VER=2.13.3
SHA=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
SRC=$HERE/src/freetype-$VER

mkdir -p "$OUT"
exec 9>"$OUT.lock"
flock 9
mkdir -p "$HERE/src"
exec 8>"$HERE/src.lock"
flock 8

if [ ! -d "$SRC" ]; then
    T=$HERE/src/freetype-$VER.tar.xz
    if [ ! -f "$T" ]; then
        curl -fsSL -o "$T" "https://download.savannah.gnu.org/releases/freetype/freetype-$VER.tar.xz" || \
        curl -fsSL -o "$T" "https://mirror.accum.se/mirror/gnu.org/savannah/freetype/freetype-$VER.tar.xz"
    fi
    echo "$SHA  $T" | sha256sum -c -
    tar -C "$HERE/src" -xf "$T"
fi
exec 8>&-

case $ARCH in
    x86_64) AFLAGS="-m64"; CFG_HOST=x86_64-linux-gnu ;;
    i686)   AFLAGS="-m32 -march=i686 -mstackrealign"; CFG_HOST=i686-linux-gnu ;;
    *) echo "usage: $0 [i686|x86_64] [output dir]"; exit 1 ;;
esac

CFLAGS="$AFLAGS -O2 -fPIC -fvisibility=hidden"

cd "$OUT"
if [ ! -f Makefile ]; then
    "$SRC/configure" --host=$CFG_HOST --prefix="$OUT/install" \
        --enable-static --disable-shared \
        --with-zlib=no --with-bzip2=no --with-png=no --with-harfbuzz=no --with-brotli=no \
        CC="gcc" CFLAGS="$CFLAGS"
fi

make -j"$(nproc)"
cp .libs/libfreetype.a "$OUT/libfreetype.a"
echo "FreeType $VER built successfully: $OUT/libfreetype.a"
