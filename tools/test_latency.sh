#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/test/latency
cc -std=gnu11 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
    tools/tests/latency.c -o build/test/latency/test
build/test/latency/test
