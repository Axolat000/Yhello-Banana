#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
JOBS=${JOBS:-2}
PREFIX=${LIBJS_PREFIX:-$HERE/build/prefix}
REF=${LIBJS_NATIVE_BUILD:-$HERE/build/native}
SOURCE=${LADYBIRD_SOURCE:-$HERE/src/ladybird-e6669482e693bda4f75cd8a90b2abd292c0f3774}
[ "$#" -eq 0 ] || { echo 'usage: build.sh (native reference prototype)' >&2; exit 2; }
python3 "$HERE/prepare_source.py"
for item in simdutf-7.5.0 simdjson-4.2.4; do
    cmake -G Ninja -S "$HERE/src/$item" -B "$HERE/build/$item-native" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DSIMDUTF_TESTS=OFF -DSIMDUTF_TOOLS=OFF -DSIMDJSON_DEVELOPER_MODE=OFF \
        -DCMAKE_INSTALL_PREFIX="$PREFIX"
    cmake --build "$HERE/build/$item-native" -j "$JOBS"
    cmake --install "$HERE/build/$item-native"
done
mkdir -p "$REF/cache/PublicSuffix"
cp "$HERE/src/public_suffix_list.dat" "$REF/cache/PublicSuffix/public_suffix_list.dat"
cmake -G Ninja -S "$HERE" -B "$REF" \
    -DCMAKE_BUILD_TYPE=Release -DLADYBIRD_SOURCE="$SOURCE" \
    -DCMAKE_PREFIX_PATH="$PREFIX" -Dsimdjson_DIR="$PREFIX/lib/cmake/simdjson"
cmake --build "$REF" -j "$JOBS" --target libjs-probe
"$REF/libjs-probe"
