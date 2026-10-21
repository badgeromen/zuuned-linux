#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-hd-artwork-api.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
ffmpeg -v error -f lavfi -i color=c=red:s=32x32 -frames:v 1 "$fixture_dir/image.jpg"
${CC:-cc} -std=c99 -D_GNU_SOURCE -DUSE_LIBAV -O1 -g -Wall -Wextra -Werror -Wno-unused-function \
 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Isrc tests/hd_artwork_api.c \
 $(pkg-config --cflags --libs libavcodec libavutil) -o "$fixture_dir/test"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/test" "$fixture_dir/image.jpg"
