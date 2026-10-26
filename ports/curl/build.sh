#!/bin/sh
# libcurl port for Banana OS.
# Builds libcurl.a statically for Banana OS applications (SDK) and test harnesses.
# Usage: ports/curl/build.sh [i686|x86_64] [output dir]
set -e

ARCH=${1:-x86_64}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SDK=$ROOT/sdk
OUT=$(realpath -m "${2:-$HERE/build/$ARCH}")

VER=8.10.1
SHA=73a4b0e99596a09fa5924a4fb7e4b995a85fda0d18a2c02ab9cf134bebce04ee
SRC=$HERE/src/curl-$VER

mkdir -p "$OUT"
exec 9>"$OUT.lock"
flock 9
mkdir -p "$HERE/src"
exec 8>"$HERE/src.lock"
flock 8

if [ ! -d "$SRC" ]; then
    T=$HERE/src/curl-$VER.tar.xz
    if [ ! -f "$T" ]; then
        curl -fsSL -o "$T" "https://curl.se/download/curl-$VER.tar.xz" || \
        curl -fsSL -o "$T" "https://github.com/curl/curl/releases/download/curl-8_10_1/curl-$VER.tar.xz"
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
        --disable-threaded-resolver \
        --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp \
        --disable-proxy --disable-dict --disable-telnet --disable-tftp \
        --disable-pop3 --disable-imap --disable-smb --disable-smtp \
        --disable-gopher --disable-mqtt --disable-manual --disable-pthreads \
        --disable-verbose --disable-sspi --disable-ntlm \
        --without-nghttp2 --without-librtmp \
        --without-ssl --without-libpsl --without-libidn2 --without-brotli \
        --without-zstd --without-zlib \
        CC="gcc" CFLAGS="$CFLAGS"
fi

make -C lib -j"$(nproc)"
cp lib/.libs/libcurl.a "$OUT/libcurl.a"
echo "libcurl $VER built successfully: $OUT/libcurl.a"
