#!/usr/bin/env bash
# zune_arm_video with real ffmpeg on a 4:4:4 source: every model profile must
# produce 8-bit 4:2:0 output at the model's size and codec.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-transcode-profiles.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
ffmpeg -nostdin -v error -y -f lavfi -i testsrc=size=640x360:rate=24:duration=2 \
    -f lavfi -i sine=frequency=330:duration=2 -c:v libx264 -preset ultrafast \
    -pix_fmt yuv444p -c:a aac -shortest "$fixture_dir/source-444.mkv"
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/transcode_profiles.c src/transcode.c src/util.c src/wiretap.c \
    -Wl,--gc-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined -pthread -o "$fixture_dir/profiles"
TMPDIR="$fixture_dir" ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/profiles" "$fixture_dir/source-444.mkv" \
    > "$fixture_dir/outputs"
declare -A expect=([zune30]="wmv2,320,240,yuv420p" [zune80]="h264,320,240,yuv420p"
                   [zunehd]="h264,480,272,yuv420p" [zunehd720]="h264,1280,720,yuv420p")
checks=0
while read -r name path; do
    got=$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,pix_fmt \
        -of csv=p=0 "$path")
    [ "$got" = "${expect[$name]}" ] || { echo "FAIL $name: got $got, expected ${expect[$name]}"; exit 1; }
    echo "PASS $name: $got"
    rm -f "$path"
    checks=$((checks + 1))
done < "$fixture_dir/outputs"
[ "$checks" = 4 ] || { echo "FAIL: $checks of 4 profiles produced output"; exit 1; }
echo "Transcode profiles: $checks profiles produce 4:2:0 from a 4:4:4 source"
