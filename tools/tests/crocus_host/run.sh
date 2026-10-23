#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Mesa's crocus on the hosted kernel i915 interface and the GPU model, for
# each generation (5, 6, 7, 7.5): an OpenGL 3.3 program (2.1 on gen 5)
# through OSMesa. Needs the reference build of CROCUS=1 ports/mesa/build.sh.
#   tools/tests/crocus_host/run.sh [frames]
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
REF=${MESA_REF:-$ROOT/ports/mesa/build-crocus/ref}
SRC=$(ls -d "$ROOT"/ports/mesa/src/mesa-*/ | head -1)
OUT=${TMPDIR:-/tmp}/crocus_host
mkdir -p "$OUT"
cc -O1 -fPIC -shared -w -I "$ROOT/kernel" -I "$ROOT/tools/tests" "$ROOT/tools/tests/crocus_host/preload.c" \
   "$ROOT/kernel/gpu_i915.c" "$ROOT/kernel/gpu_i915_lrc.c" "$ROOT/kernel/gpu_gem.c" -o "$OUT/preload.so" -ldl -lpthread
cc -O1 -w -I "$SRC/include" "$ROOT/tools/tests/crocus_host/gltri.c" -L "$REF/src/gallium/targets/osmesa" -lOSMesa \
   -Wl,-rpath,"$REF/src/gallium/targets/osmesa" -o "$OUT/gltri"
fail=0
for g in 5 6 7 75; do
    echo "== gen $g"
    MODEL_GEN=$g MODEL_QUIET=1 GALLIUM_DRIVER=crocus LD_PRELOAD="$OUT/preload.so" timeout 600 "$OUT/gltri" "${1:-30}" || fail=$((fail + 1))
done
[ $fail = 0 ] && echo "crocus on the model: all 4 generations ran" || { echo "crocus on the model: $fail generation(s) failed"; exit 1; }
