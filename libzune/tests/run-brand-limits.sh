#!/usr/bin/env bash
# zune_get_brand_limits: descriptor parsing, caching and failures, with
# ptp_transaction replaced by an in-memory device. No USB.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-brand-limits.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/brand-limits.c src/wiretap.c src/thumbnail.c src/mtp.c src/util.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/brand-limits"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    "$fixture_dir/brand-limits"
