#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-hd-art-gallery.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -DUSE_LIBAV -O1 -g -Wall -Wextra -Werror -Wno-unused-function \
 -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Isrc \
 $(pkg-config --cflags libavcodec libavutil) tests/hd_art_gallery.c -Wl,--gc-sections \
 $(pkg-config --libs libavcodec libavutil) -o "$fixture_dir/test"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/test"
