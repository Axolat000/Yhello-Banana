#!/usr/bin/env bash
# Host checks with the same TinyCC vector patch and setjmp used by Banana OS.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
arch=${1:-x86_64}
case "$arch" in
    x86_64) bits=64; target=X86_64 ;;
    i686) bits=32; target=I386 ;;
    *) echo "usage: $0 [x86_64|i686]" >&2; exit 2 ;;
esac
source_dir="$repo/ports/tcc/src/host"
if [[ ! -f "$source_dir/libtcc.c" ]]; then
    echo 'Run ports/tcc/build.sh first to prepare the TinyCC sources.' >&2
    exit 1
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/banana-cltest.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
cp -R "$source_dir" "$work/tcc"
mkdir -p "$work/include/CL"
cp "$repo"/sdk/include/CL/*.h "$work/include/CL/"
python3 "$repo/sdk/lib/patch-cljit.py" "$work/tcc"
cc=${CC:-gcc}
"$cc" -m"$bits" -O2 -shared -fPIC -DONE_SOURCE=1 -D"TCC_TARGET_$target"=1 -DCONFIG_TCC_STATIC=1 \
    -I"$work/tcc" "$work/tcc/libtcc.c" -o "$work/libtcc.so" -ldl -lm
"$cc" -m"$bits" -O2 -DBCL_HOST -include "$repo/sdk/include/setjmp.h" \
    -I"$work/include" -I"$work/tcc" "$repo/sdk/examples/cltest/cltest.c" \
    "$repo/sdk/lib/cl.c" "$repo/sdk/lib/setjmp.c" \
    -L"$work" -Wl,-rpath,"$work" -ltcc -lm -lpthread -ldl -o "$work/cltest"
cd -- "$work"
for threads in 1 4; do
    echo "== OpenCL $arch: $threads worker(s)"
    BCL_THREADS=$threads ./cltest
done
