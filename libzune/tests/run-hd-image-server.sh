#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-hd-image-server.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -O1 -g -Wall -Wextra -Werror -Wno-unused-function \
 -fsanitize=address,undefined -fno-sanitize-recover=undefined $(pkg-config --cflags libxml-2.0) tests/hd_image_server.c \
 $(pkg-config --libs libxml-2.0) -o "$fixture_dir/test"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/test"
