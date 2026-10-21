#!/usr/bin/env bash
# T3.2: uploads stream from the file instead of a file-sized buffer. Real
# upload/MTP/PTP code over a bulk-level fake device; no USB.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-upload-stream.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/upload-stream.c src/mtp.c src/ptp.c src/wiretap.c \
    -Wl,--gc-sections -Wl,--wrap=malloc -fsanitize=address,undefined -o "$fixture_dir/upload-stream"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$fixture_dir/upload-stream"
