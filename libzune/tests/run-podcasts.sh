#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-podcasts.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/podcasts.c src/wiretap.c src/podcast.c src/upload.c tests/ptp_file_adapter.c src/mtp.c \
    -Wl,--gc-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$fixture_dir/podcasts"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/podcasts"
