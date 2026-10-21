#!/usr/bin/env bash
# Log routing: tap delivery, levels, SILENT, ZUNE_CHATTER, default format.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-wiretap.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections -pthread \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/wiretap.c src/wiretap.c src/mtp.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/wiretap"
export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
env -u ZUNE_CHATTER "$fixture_dir/wiretap"
ZUNE_CHATTER=scream "$fixture_dir/wiretap" env-scream
ZUNE_CHATTER=WHISPER "$fixture_dir/wiretap" env-whisper
