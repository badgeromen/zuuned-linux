#!/usr/bin/env bash
# An aborted transfer cancels the device's PTP transaction instead of leaving
# the device mid data phase. Scripted USB backend; no USB.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-ptp-cancel.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/ptp-cancel.c src/ptp.c src/wiretap.c \
    -Wl,--wrap=clock_gettime -fsanitize=address,undefined -o "$fixture_dir/ptp-cancel"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$fixture_dir/ptp-cancel"
