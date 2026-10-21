#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-podcast-state.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Isrc \
    tests/podcast-state.c src/wiretap.c src/podcast_state.c src/zmdb_io.c \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$fixture_dir/podcast-state"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/podcast-state" "$@"
