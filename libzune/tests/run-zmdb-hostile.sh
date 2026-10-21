#!/usr/bin/env bash
# Hostile ZMDB databases: every malformed blob fails closed, no sanitizer
# report. Set ZUNE_ZMDB_SAMPLE to a local (never committed) device dump to
# also prove a real database still parses.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-zmdb-hostile.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/zmdb-hostile.c src/wiretap.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/zmdb-hostile"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    "$fixture_dir/zmdb-hostile" "${ZUNE_ZMDB_SAMPLE:-}" 2> "$fixture_dir/stderr" || {
        cat "$fixture_dir/stderr" >&2; exit 1; }
if grep -E "runtime error|AddressSanitizer|LeakSanitizer" "$fixture_dir/stderr"; then
    echo "FAIL: sanitizer report" >&2
    exit 1
fi
