#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
gcc -O2 -g -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -fsanitize=address,undefined --param asan-globals=0 \
    -I kernel -I shell -I net -I crypto -I usb -I web -I media \
    tools/tests/hostname.c kernel/kstring.c -o "$out/test"
"$out/test"

# Hide SDK definitions from the host's dynamic linker: only the functions
# reached by the test should be linked, rather than replacing host libc.
echo '{ local: *; };' > "$out/sdk.map"
gcc -O2 -g -fno-builtin -ffunction-sections -fdata-sections \
    -Wl,--gc-sections,--version-script="$out/sdk.map" \
    -fsanitize=address,undefined --param asan-globals=0 \
    -I sdk/include tools/tests/hostname_sdk.c sdk/lib/socket.c sdk/lib/unix.c \
    -o "$out/sdk-test"
"$out/sdk-test"
