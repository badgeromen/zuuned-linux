#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-video-test.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra \
    -Wno-unused-parameter -Wno-unused-function \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined \
    -Iinclude -Isrc tests/video_named.c src/video.c src/mtp.c src/finalize.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/video_named"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/video_named"
