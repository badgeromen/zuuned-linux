#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-artist-reuse.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined \
    -Iinclude -Isrc tests/artist_reuse.c src/album.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/artist-reuse"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/artist-reuse"
