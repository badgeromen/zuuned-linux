#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-audiobook-protocol.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-function -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -Iinclude -Isrc tests/audiobook-protocol.c src/wiretap.c src/audiobook.c src/upload.c tests/ptp_file_adapter.c src/mtp.c \
    -Wl,--gc-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$fixture_dir/audiobook-protocol"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/audiobook-protocol"
