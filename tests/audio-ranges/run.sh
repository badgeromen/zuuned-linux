#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
range_build=$(mktemp -d /tmp/zuuned-audio-ranges.XXXXXX)
trap 'rm -rf -- "$range_build"' EXIT
read -r -a range_flags <<< "$(pkg-config --cflags --libs libavformat libavcodec libavutil libswresample libswscale)"
${CC:-cc} -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Itranscode \
    tests/audio-ranges/driver.c transcode/libav_transcode.c transcode/id3_rewrite.c \
    "${range_flags[@]}" -lmp3lame -lm -o "$range_build/test"
python3 tests/audio-ranges/verify.py "$range_build/test" "$range_build"
