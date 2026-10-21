#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-art-journal.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-function \
 -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Isrc \
 tests/hd_art_journal.c -Wl,--gc-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$fixture_dir/test"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/test"
