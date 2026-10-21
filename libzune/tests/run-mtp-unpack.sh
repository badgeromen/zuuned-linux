#!/usr/bin/env bash
# Device-chosen MTP array counts: wrapping counts are refused, no sanitizer
# report; valid replies keep their values. ptp_transaction is a fixture.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-mtp-unpack.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/mtp-unpack.c src/wiretap.c src/mtp.c src/devinfo.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/mtp-unpack"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    "$fixture_dir/mtp-unpack"
