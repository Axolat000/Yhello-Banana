#!/bin/sh
# libcurl port for Banana OS.
# Builds libcurl.a statically for Banana OS applications (SDK) and test harnesses,
# with ported Mbed TLS backend enabled for HTTPS transfers and system CA verification.
# Usage: ports/curl/build.sh [i686|x86_64] [output dir]
set -e

ARCH=${1:-x86_64}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SDK=$ROOT/sdk
OUT=$(realpath -m "${2:-$HERE/build/$ARCH}")

VER=8.10.1
SHA=73a4b0e99596a09fa5924a4fb7e4b995a85fda0d18a2c02ab9cf134bebce04ee
SRC=${CURL_SRC:-$HERE/src/curl-$VER}

mkdir -p "$OUT"
exec 9>"$OUT.lock"
flock 9
mkdir -p "$(dirname "$SRC")"
exec 8>"$(dirname "$SRC").lock"
flock 8

if [ ! -d "$SRC" ]; then
    T="$(dirname "$SRC")/curl-$VER.tar.xz"
    if [ ! -f "$T" ]; then
        curl -fsSL -o "$T" "https://curl.se/download/curl-$VER.tar.xz" || \
        curl -fsSL -o "$T" "https://github.com/curl/curl/releases/download/curl-8_10_1/curl-$VER.tar.xz"
    fi
    echo "$SHA  $T" | sha256sum -c -
    tar -C "$(dirname "$SRC")" -xf "$T"
fi
exec 8>&-

case $ARCH in
    x86_64) AFLAGS="-m64"; CFG_HOST=x86_64-linux-gnu ;;
    i686)   AFLAGS="-m32 -march=i686 -mstackrealign"; CFG_HOST=i686-linux-gnu ;;
    *) echo "usage: $0 [i686|x86_64] [output dir]"; exit 1 ;;
esac

CFLAGS="$AFLAGS -O2 -fPIC -fvisibility=hidden"

# 1. Build ported Mbed TLS static libraries if not present in $OUT/mbedtls
MBED_DIR="$OUT/mbedtls"
if [ ! -f "$MBED_DIR/lib/libmbedtls.a" ] || [ ! -f "$MBED_DIR/lib/libmbedx509.a" ] || [ ! -f "$MBED_DIR/lib/libmbedcrypto.a" ]; then
    rm -rf "$MBED_DIR"
    mkdir -p "$MBED_DIR/include" "$MBED_DIR/lib" "$MBED_DIR/objs"
    cp -r "$ROOT/third_party/mbedtls/include"/* "$MBED_DIR/include/"

    for s in "$ROOT/third_party/mbedtls/library"/*.c; do
        bname=$(basename "$s" .c)
        gcc $CFLAGS -I "$MBED_DIR/include" -c "$s" -o "$MBED_DIR/objs/$bname.o"
    done

    ar rcs "$MBED_DIR/lib/libmbedtls.a" "$MBED_DIR/objs"/ssl_*.o
    ar rcs "$MBED_DIR/lib/libmbedx509.a" "$MBED_DIR/objs"/x509*.o
    ar rcs "$MBED_DIR/lib/libmbedcrypto.a" "$MBED_DIR/objs"/*.o
    rm -rf "$MBED_DIR/objs"
fi

cd "$OUT"
if [ ! -f Makefile ]; then
    "$SRC/configure" --host=$CFG_HOST --prefix="$OUT/install" \
        --enable-static --disable-shared \
        --with-mbedtls="$MBED_DIR" \
        --with-ca-bundle="/etc/ssl/certs/ca-certificates.crt" \
        --disable-threaded-resolver \
        --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp \
        --disable-proxy --disable-dict --disable-telnet --disable-tftp \
        --disable-pop3 --disable-imap --disable-smb --disable-smtp \
        --disable-gopher --disable-mqtt --disable-manual --disable-pthreads \
        --disable-verbose --disable-sspi --disable-ntlm \
        --without-nghttp2 --without-librtmp \
        --without-libpsl --without-libidn2 --without-brotli \
        --without-zstd --without-zlib \
        CC="gcc" CFLAGS="$CFLAGS"
fi

make -C lib -j"$(nproc)"

# Bundle libcurl and mbedtls static members into standalone libcurl.a
UNPACK_TMP="$OUT/unpack_objs"
rm -rf "$UNPACK_TMP"
mkdir -p "$UNPACK_TMP"
cd "$UNPACK_TMP"
ar x "$OUT/lib/.libs/libcurl.a"
ar x "$MBED_DIR/lib/libmbedtls.a"
ar x "$MBED_DIR/lib/libmbedx509.a"
ar x "$MBED_DIR/lib/libmbedcrypto.a"
ar rcs "$OUT/libcurl.a" *.o
cd "$OUT"
rm -rf "$UNPACK_TMP"

echo "libcurl $VER (with bundled Mbed TLS backend) built successfully: $OUT/libcurl.a"
