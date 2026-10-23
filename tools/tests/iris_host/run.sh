#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Mesa's iris on the hosted kernel i915 interface and the GPU model, for each
# generation it drives (8 Broadwell, 9 Skylake, 9.5 Coffee Lake, 11 Ice Lake):
# the OpenGL 3.3 program of crocus_host (its preload library and gltri)
# through OSMesa. Needs the reference build of IRIS=1 ports/mesa/build.sh.
#   [GENS="8 11"] tools/tests/iris_host/run.sh [frames]
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
REF=${MESA_REF:-$ROOT/ports/mesa/build-iris/ref}
SRC=$(ls -d "$ROOT"/ports/mesa/src/mesa-*/ | head -1)
OUT=${TMPDIR:-/tmp}/iris_host
H=$ROOT/tools/tests/crocus_host
mkdir -p "$OUT"
cc -O1 -fPIC -shared -w -I "$ROOT/kernel" -I "$ROOT/tools/tests" "$H/preload.c" \
   "$ROOT/kernel/gpu_i915.c" "$ROOT/kernel/gpu_i915_lrc.c" "$ROOT/kernel/gpu_gem.c" -o "$OUT/preload.so" -ldl -lpthread
cc -O1 -w -I "$SRC/include" "$H/gltri.c" -L "$REF/src/gallium/targets/osmesa" -lOSMesa \
   -Wl,-rpath,"$REF/src/gallium/targets/osmesa" -o "$OUT/gltri"
fail=0
for g in ${GENS:-8 9 95 11}; do
    echo "== gen $g"
    MODEL_GEN=$g MODEL_QUIET=1 GALLIUM_DRIVER=iris LD_PRELOAD="$OUT/preload.so" timeout 600 "$OUT/gltri" "${1:-30}" || fail=$((fail + 1))
done
[ $fail = 0 ] && echo "iris on the model: all 4 generations ran" || { echo "iris on the model: $fail generation(s) failed"; exit 1; }
