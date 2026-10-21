#!/usr/bin/env bash
# T3.3: album and playlist forges clean up and fail when their commit or
# track references fail. In-memory device; no USB.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-forge-partial.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/forge-partial.c src/album.c src/playlist.c src/mtp.c src/wiretap.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/forge-partial"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$fixture_dir/forge-partial"
