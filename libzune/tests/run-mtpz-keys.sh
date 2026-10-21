#!/usr/bin/env bash
# T1.6: MTPZ keys load once, are wiped by zune_go_dark, and libgcrypt setup is
# left to a host that did it first. Mock key file with fake values; no USB.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-mtpz-keys.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
mkdir "$fixture_dir/home"
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -pthread \
    -Iinclude -Isrc tests/mtpz-keys.c src/mtpz.c src/wiretap.c \
    -Wl,--gc-sections -Wl,--wrap=fopen -Wl,--wrap=free \
    -Wl,--wrap=gcry_control -Wl,--wrap=gcry_check_version \
    -fsanitize=address,undefined -lgcrypt -o "$fixture_dir/mtpz-keys"
for mode in keys gcrypt-fresh gcrypt-host; do
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
        "$fixture_dir/mtpz-keys" "$fixture_dir/home" "$mode" 2>/dev/null
done
