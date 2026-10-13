#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-spoken-inventory.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -Iinclude -Isrc \
    tests/spoken-inventory.c src/spoken_inventory.c \
    -fsanitize=address,undefined -o "$fixture_dir/spoken-inventory"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/spoken-inventory"
