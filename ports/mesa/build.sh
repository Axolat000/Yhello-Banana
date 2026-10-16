#!/bin/sh
# Builds OpenGL 3.3 for Banana OS: Mesa's libOSMesa.so (the softpipe driver,
# drawing on the processor; 64-bit), which <GL/gl3.h> apps (USE_GL3 = 1)
# load from /lib, /usr/lib or /mnt/<disk>/lib.
#     ports/mesa/build.sh [output dir]      (default: ports/mesa/build)
# Out: <output>/libOSMesa.so, gl3test.bpk, and gl3disk.img - a FAT32 disk
# with lib/libOSMesa.so and gl3test.bpk for QEMU (-drive file=...,if=virtio).
#
# How: Mesa is first built for Linux with the same options (the reference
# build); then every object of libOSMesa.so is compiled again from that
# build's compile_commands.json with the SDK's headers (rebuild.py) and
# the library is linked without the system's C library.
# Needs: gcc, meson, ninja, flex, bison, python3-mako, python3-yaml, mtools,
# sfdisk.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(realpath -m "${1:-$HERE/build}")
VER=24.3.4
SHA=e641ae27191d387599219694560d221b7feaa91c900bcec46bf444218ed66025
SRC=${MESA_SRC:-$HERE/src/mesa-$VER}
REF=${MESA_REF:-$OUT/ref}                # (MESA_SRC, MESA_REF: a source / reference build already there)
JOBS=${JOBS:-$(nproc)}
mkdir -p "$HERE/src" "$OUT"

if [ ! -d "$SRC" ]; then
    T=$HERE/src/mesa-$VER.tar.xz
    [ -f "$T" ] || curl -fsSL -o "$T" https://archive.mesa3d.org/mesa-$VER.tar.xz
    echo "$SHA  $T" | sha256sum -c -
    tar -C "$HERE/src" -xJf "$T"
fi

# ── the reference Linux build ─────────────────────────────────────────
if [ ! -f "$REF/src/gallium/targets/osmesa/libOSMesa.so.8.0.0" ]; then
    [ -f "$REF/build.ninja" ] || meson setup "$REF" "$SRC" -Dbuildtype=release -Dosmesa=true \
        -Dgallium-drivers=softpipe -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dplatforms= \
        -Dllvm=disabled -Dgles1=disabled -Dgles2=disabled -Dzstd=disabled -Dvalgrind=disabled \
        -Dlibunwind=disabled -Dshader-cache=disabled -Dlmsensors=disabled -Dgallium-va=disabled \
        -Dgallium-vdpau=disabled -Dgallium-xa=disabled -Dgallium-nine=false -Dgallium-opencl=disabled \
        -Dgallium-rusticl=false -Dandroid-libbacktrace=disabled -Dexpat=disabled -Dxmlconfig=disabled \
        -Ddraw-use-llvm=false -Dbuild-tests=false -Dtools=
    ninja -C "$REF" -j"$JOBS"
fi

# ── libOSMesa.so for Banana OS ────────────────────────────────────────
JOBS=$JOBS python3 "$HERE/rebuild.py" "$REF" "$OUT/banana"
cp "$OUT/banana/libOSMesa.so" "$OUT/libOSMesa.so"

# ── the test app and a test disk ──────────────────────────────────────
make -C "$HERE/gl3test"
cp "$HERE/gl3test/gl3test.bpk" "$OUT/"
IMG=$OUT/gl3disk.img
rm -f "$IMG"
truncate -s 64M "$IMG"
printf 'label: dos\nstart=2048, type=c\n' | /sbin/sfdisk -q "$IMG"
mformat -i "$IMG@@1M" -F -v GL3 ::
mmd -i "$IMG@@1M" ::/lib
mcopy -i "$IMG@@1M" "$OUT/libOSMesa.so" ::/lib/
mcopy -i "$IMG@@1M" "$OUT/gl3test.bpk" ::/
echo "OpenGL 3.3 for Banana OS: $OUT/libOSMesa.so ($(du -h "$OUT/libOSMesa.so" | cut -f1)), test disk $IMG"
