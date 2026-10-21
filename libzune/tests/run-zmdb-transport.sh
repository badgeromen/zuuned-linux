#!/usr/bin/env bash
# GetZMDB (0x9217) transport against a scripted USB pipe: every exchange must
# consume the device's response, through a ZLP or a late answer.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-zmdb-transport.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Isrc \
    tests/zmdb-transport.c src/wiretap.c src/zmdb_io.c \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -pthread -o "$fixture_dir/zmdb-transport"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/zmdb-transport"
