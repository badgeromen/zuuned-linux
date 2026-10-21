#!/usr/bin/env bash
# T3.1: the shared upload engine's retry, cleanup and policy decisions over a
# scripted device. No USB.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-upload-engine.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/upload-engine.c src/upload.c tests/ptp_file_adapter.c src/mtp.c src/wiretap.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/upload-engine"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$fixture_dir/upload-engine"
