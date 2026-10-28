#!/bin/sh
# HarfBuzz 8.5.0 port for Banana OS.
# Builds libharfbuzz.a statically for Banana OS applications (SDK) and characterization tests.
# Usage: ports/harfbuzz/build.sh [i686|x86_64] [output dir]
set -e

ARCH=${1:-x86_64}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SDK=$ROOT/sdk
OUT=$(realpath -m "${2:-$HERE/build/$ARCH}")

VER=8.5.0
SHA=77e4f7f98f3d86bf8788b53e6832fb96279956e1c3961988ea3d4b7ca41ddc27
SRC=$HERE/src/harfbuzz-$VER

# Ensure FreeType is built first for this architecture
FT_DIR=$ROOT/ports/freetype
"$FT_DIR/build.sh" "$ARCH" "$FT_DIR/build/$ARCH"
FT_INC=$FT_DIR/src/freetype-2.13.3/include
FT_LIB=$FT_DIR/build/$ARCH/libfreetype.a

mkdir -p "$OUT"
exec 9>"$OUT.lock"
flock 9
mkdir -p "$HERE/src"
exec 8>"$HERE/src.lock"
flock 8

if [ ! -d "$SRC" ]; then
    T=$HERE/src/harfbuzz-$VER.tar.xz
    if [ ! -f "$T" ]; then
        curl -fsSL -o "$T" "https://github.com/harfbuzz/harfbuzz/releases/download/$VER/harfbuzz-$VER.tar.xz"
    fi
    echo "$SHA  $T" | sha256sum -c -
    tar -C "$HERE/src" -xf "$T"
fi
exec 8>&-

case $ARCH in
    x86_64)
        AFLAGS="-m64"
        CFG_HOST="x86_64-linux-gnu"
        ;;
    i686)
        AFLAGS="-m32 -march=i686 -mstackrealign"
        CFG_HOST="i686-linux-gnu"
        ;;
    *)
        echo "usage: $0 [i686|x86_64] [output dir]"
        exit 1
        ;;
esac

CFLAGS="$AFLAGS -O2 -fPIC -fvisibility=hidden -I$FT_INC"
CXXFLAGS="$AFLAGS -O2 -fPIC -fvisibility=hidden -I$FT_INC"
LDFLAGS="$AFLAGS"

cd "$OUT"
if [ ! -f Makefile ]; then
    "$SRC/configure" --host="$CFG_HOST" --prefix="$OUT/install" \
        --enable-static --disable-shared \
        --with-glib=no --with-gobject=no --with-cairo=no --with-chafa=no \
        --with-icu=builtin --with-graphite2=no \
        --with-freetype=yes \
        FREETYPE_CFLAGS="-I$FT_INC" \
        FREETYPE_LIBS="-L$FT_DIR/build/$ARCH -lfreetype" \
        CC="gcc" CXX="g++" \
        CFLAGS="$CFLAGS" CXXFLAGS="$CXXFLAGS" LDFLAGS="$LDFLAGS"
fi

make -C src libharfbuzz.la -j"$(nproc)"
cp src/.libs/libharfbuzz.a "$OUT/libharfbuzz.a"
echo "HarfBuzz $VER built successfully: $OUT/libharfbuzz.a"
