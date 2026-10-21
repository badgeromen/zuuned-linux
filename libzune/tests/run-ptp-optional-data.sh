#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-ptp-optional.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/ptp_optional_data.c src/wiretap.c src/ptp.c \
    -Wl,--gc-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$fixture_dir/probe"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/probe"
