#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-hd-art-first-sync.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-function \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/hd_art_first_sync.c -Wl,--gc-sections -o "$fixture_dir/hd-art-first-sync"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/hd-art-first-sync"
