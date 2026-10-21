#!/usr/bin/env bash
# Failed queries and operations report errors; ZUNE_PL_FMT only in knob builds.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-honest-failures.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
sources=(tests/honest-failures.c src/wiretap.c src/device.c src/track.c src/upload.c src/photo.c
    src/video.c src/playlist.c src/album.c src/mtp.c src/util.c src/finalize.c src/thumbnail.c)
flags=(-std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter
    -Wno-unused-function -ffunction-sections -fdata-sections -pthread
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Isrc)
${CC:-cc} "${flags[@]}" "${sources[@]}" -Wl,--gc-sections -o "$fixture_dir/shipping"
${CC:-cc} "${flags[@]}" -DZUNE_DEBUG_KNOBS "${sources[@]}" -Wl,--gc-sections -o "$fixture_dir/knobs"
export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
"$fixture_dir/shipping"
"$fixture_dir/knobs" | tail -2
