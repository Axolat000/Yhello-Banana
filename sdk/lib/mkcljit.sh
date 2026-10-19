#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# mkcljit.sh TCC_PORT_DIR OUT_DIR - TinyCC's Banana OS source (ports/tcc,
# made by its build.sh) copied to OUT_DIR for in-memory compilation and
# the OpenCL vector extension. Banana Code keeps its own compiler unchanged.
# sdk/banana.mk runs it for apps with USE_CL = 1.
set -e
TCC=$1
OUT=$2
SRC=$TCC/src/banana
if [ ! -f "$SRC/libtcc.c" ]; then
    echo "mkcljit: getting TinyCC first ($TCC/build.sh)"
    "$TCC/build.sh" "$TCC/build"
fi
mkdir -p "$OUT"
cp -r "$SRC"/. "$OUT"/
sed -i 's|^#define CONFIG_TCC_NO_NATIVE 1.*|/* (in memory: OpenCL kernels run inside the app - sdk/lib/mkcljit.sh) */|' "$OUT/config.h"
grep -q CONFIG_TCC_NO_NATIVE "$OUT/config.h" && { echo "mkcljit: could not change $OUT/config.h" >&2; exit 1; }
echo "mkcljit: $OUT"
# tccrun.c protects the pages it fills: on Banana OS app memory can run as it is
mkdir -p "$OUT/sys"
cat > "$OUT/sys/mman.h" <<'H'
/* (mkcljit.sh) what TinyCC's tccrun.c needs: app memory runs as it is */
#ifndef BCL_SYS_MMAN_H
#define BCL_SYS_MMAN_H
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
static inline int mprotect(void* p, unsigned long n, int mode) { (void)p; (void)n; (void)mode; return 0; }
#endif
H

python3 "$(dirname "$0")/patch-cljit.py" "$OUT"
