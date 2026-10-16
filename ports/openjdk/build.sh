#!/bin/sh
# Builds OpenJDK 17 (the Zero interpreter, 64-bit) for Banana OS:
#     ports/openjdk/build.sh [output dir]      (default: ports/openjdk/build)
# Out: <output>/jdk - a JDK image with java.base (copy it to /jdk, or a
# disk's jdk folder: the java app finds /jdk, /mnt/usb/jdk, /mnt/virtio/jdk,
# /mnt/sata/jdk, or JAVA_HOME) - and <output>/jdkdisk.img, a FAT32 disk with
# jdk/, java.bpk and Hello.class for QEMU (-drive file=...,if=virtio).
#
# How: OpenJDK is first built for Linux (configure + make images, the
# reference build, ~1 h); then every C/C++ object of HotSpot and of
# java.base's native libraries is compiled again from that build's command
# lines with the Banana OS SDK's headers (rebuild.py), and linked into
# shared libraries that the SDK's dlopen loads in the java app. The Java
# classes (lib/modules) are the reference build's: java.base runs on the
# SDK's Linux-like C library as it does on Linux.
# Needs: the reference build's tools (gcc/g++, make, autoconf, zip, the X11,
# CUPS, fontconfig, ALSA headers are not needed: headless), libffi-dev,
# mtools and sfdisk for the disk.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(realpath -m "${1:-$HERE/build}")
VER=17.0.20.1
SRC_SHA=8e5f18f6f75a759fc9584c3c2f8c44a5737692f3ea348cc1f9e63704b9dd81ee
BOOT_SHA=3808d1d15e3ec6bd5b84057fb5d84c33d8a1536a258146bcea2e603fc726e08e
DL=$HERE/src
SRC=${JDK_SRC:-$DL/jdk17u-jdk-$VER-ga}       # (JDK_SRC, BOOT_JDK: ones already there)
BOOT=${BOOT_JDK:-$DL/jdk-$VER+1}
REFB=$SRC/build/linux-x86_64-zero-release
JOBS=${JOBS:-$(nproc)}
mkdir -p "$DL" "$OUT"

# ── the sources and a boot JDK (Temurin) ──────────────────────────────
if [ ! -d "$SRC" ]; then
    T=$DL/jdk17u-$VER.tar.gz
    [ -f "$T" ] || curl -fsSL -o "$T" https://github.com/openjdk/jdk17u/archive/refs/tags/jdk-$VER-ga.tar.gz
    echo "$SRC_SHA  $T" | sha256sum -c -
    tar -C "$DL" -xzf "$T"
fi
if [ ! -d "$BOOT" ]; then
    T=$DL/temurin-$VER.tar.gz
    [ -f "$T" ] || curl -fsSL -o "$T" "https://github.com/adoptium/temurin17-binaries/releases/download/jdk-$VER%2B1/OpenJDK17U-jdk_x64_linux_hotspot_${VER}_1.tar.gz"
    echo "$BOOT_SHA  $T" | sha256sum -c -
    tar -C "$DL" -xzf "$T"
fi

# ── the reference Linux build (its command lines and its classes) ─────
if [ ! -f "$REFB/images/jdk/lib/modules" ]; then
    (cd "$SRC" && sh configure --with-boot-jdk="$BOOT" --with-jvm-variants=zero --enable-headless-only \
        --with-debug-level=release --disable-warnings-as-errors --with-native-debug-symbols=none \
        --with-jobs="$JOBS" --with-version-opt=banana --with-conf-name=linux-x86_64-zero-release)
    make -C "$SRC" CONF=linux-x86_64-zero-release images
fi

# ── HotSpot and java.base's native code, for Banana OS ────────────────
W=$OUT/work
N=$REFB/support/native/java.base
mkdir -p "$W" "$OUT/lib/server"
JOBS=$JOBS python3 "$HERE/rebuild.py" "$REFB/hotspot/variant-zero/libjvm/objs" "$W/hotspot"
for l in libfdlibm libjava libnio libnet libjimage libverify libzip; do
    extra=
    [ $l = libzip ] && extra=-I$SRC/src/java.base/share/native/libzip/zlib
    [ $l = libjimage ] && extra="-fno-exceptions -fno-rtti"       # (the SDK has no C++ unwinder)
    echo "== $l"
    EXTRA="$extra" JOBS=$JOBS python3 "$HERE/rebuild.py" "$N/$l" "$W/$l"
done
# zlib (bundled with OpenJDK; libzip's own copy)
mkdir -p "$W/zlib"
for c in "$SRC"/src/java.base/share/native/libzip/zlib/*.c; do
    gcc -nostdinc -I"$HERE/../../sdk/include" -isystem "$(gcc -print-file-name=include)" -D__BANANA_OS__ \
        -mno-red-zone -fno-stack-protector -O2 -fPIC -fvisibility=hidden -DHAVE_UNISTD_H \
        -c "$c" -o "$W/zlib/$(basename "$c" .c).o"
done
rm -f "$W/libfdlibm.a"
ar rcs "$W/libfdlibm.a" "$W"/libfdlibm/objs/*.o

LIBGCC=$(gcc -print-libgcc-file-name)
L=$OUT/lib
# libjvm: crtbeginS/crtendS give it its own __dso_handle (C++ statics' destructors)
g++ -shared -nostdlib -Wl,-soname,libjvm.so \
    -Wl,--version-script="$REFB/hotspot/variant-zero/libjvm/mapfile" -o "$L/server/libjvm.so" \
    "$(gcc -print-file-name=crtbeginS.o)" "$W"/hotspot/objs/*.o /usr/lib/x86_64-linux-gnu/libffi.a "$LIBGCC" \
    "$(gcc -print-file-name=crtendS.o)"
so() {   # so NAME objects/libraries...
    n=$1; shift
    gcc -shared -nostdlib -Wl,-soname,$n.so -Wl,--exclude-libs,ALL -o "$L/$n.so" "$@" -L"$L" -L"$L/server" "$LIBGCC"
}
so libjava "$W"/libjava/objs/*.o "$W/libfdlibm.a" -ljvm
so libnet "$W"/libnet/objs/*.o -ljvm -ljava
so libnio "$W"/libnio/objs/*.o -ljava -lnet
so libjimage "$W"/libjimage/objs/*.o -ljvm
so libverify "$W"/libverify/objs/*.o -ljvm
so libzip "$W"/libzip/objs/*.o "$W"/zlib/*.o -ljvm

# ── the image: java.base (jlink), with the Banana OS libraries ────────
rm -rf "$OUT/jdk"
"$REFB/images/jdk/bin/jlink" --add-modules java.base --strip-debug --no-header-files --no-man-pages --output "$OUT/jdk"
rm -rf "$OUT/jdk/bin" "$OUT/jdk/lib/jexec" "$OUT/jdk/lib/jspawnhelper" "$OUT/jdk/lib/libjli.so" \
       "$OUT/jdk/lib/libjsig.so" "$OUT/jdk/lib/server/libjsig.so"
cp "$L"/*.so "$OUT/jdk/lib/"
cp "$L/server/libjvm.so" "$OUT/jdk/lib/server/"

# ── the java app and a test disk ──────────────────────────────────────
make -C "$HERE/launcher"
S=$OUT/disk
rm -rf "$S" && mkdir -p "$S"
cp -r "$OUT/jdk" "$S/jdk"
cp "$HERE/launcher/java.bpk" "$S/"
"$BOOT/bin/javac" --release 17 -d "$S" "$HERE/test/Hello.java"
IMG=$OUT/jdkdisk.img
rm -f "$IMG"
truncate -s 256M "$IMG"
printf 'label: dos\nstart=2048, type=c\n' | /sbin/sfdisk -q "$IMG"
mformat -i "$IMG@@1M" -F -v JDK ::
mcopy -s -i "$IMG@@1M" "$S"/* ::/
echo "OpenJDK for Banana OS: $OUT/jdk ($(du -sh "$OUT/jdk" | cut -f1)), test disk $IMG"
