#!/usr/bin/env bash
# zune_arm_photo and zune_snap_thumb with real ffmpeg: an animated GIF yields
# one JPEG, a clip shorter than the 10-second seek still gets a thumbnail, and
# an undecodable file fails without leaving an output file.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-media-prep.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
ff() { ffmpeg -nostdin -v error -y "$@"; }
ff -f lavfi -i "color=c=red:size=300x200:rate=5:duration=0.6" -loop 0 "$fixture_dir/animated.gif"
ff -f lavfi -i "testsrc=size=800x600:rate=1" -frames:v 1 "$fixture_dir/still.png"
ff -f lavfi -i testsrc=size=640x360:rate=24:duration=4 -c:v libx264 -preset ultrafast \
    -pix_fmt yuv420p "$fixture_dir/short.mp4"
ff -f lavfi -i testsrc=size=640x360:rate=24:duration=12 -c:v libx264 -preset ultrafast \
    -pix_fmt yuv420p "$fixture_dir/long.mp4"
printf 'not a video' > "$fixture_dir/not-video.mp4"
frames=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames \
    -of csv=p=0 "$fixture_dir/animated.gif")
[ "$frames" -gt 1 ] || { echo "FAIL: fixture GIF has $frames frame(s)"; exit 1; }
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/media_prep.c src/photo.c src/upload.c src/util.c src/wiretap.c \
    -Wl,--gc-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined -pthread -o "$fixture_dir/media-prep"
TMPDIR="$fixture_dir" ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/media-prep" \
    "$fixture_dir/animated.gif" "$fixture_dir/still.png" "$fixture_dir/short.mp4" \
    "$fixture_dir/long.mp4" "$fixture_dir/not-video.mp4" "$fixture_dir" > "$fixture_dir/outputs"
declare -A expect=([animated-gif]="mjpeg,300,200" [still-png]="mjpeg,480,360"
                   [short-clip]="mjpeg,200,113" [long-clip]="mjpeg,200,113")
checks=0 failed=0
while read -r kind name path extra; do
    if [ "$kind" = failure ]; then
        set -- $path $extra
        if [ "$1" = -1 ] && [ "$2" = 0 ]; then echo "PASS $name: fails and leaves no output"
        else echo "FAIL $name: rc $1, output left $2"; failed=$((failed + 1)); fi
        checks=$((checks + 1))
        continue
    fi
    got=-
    [ "$path" != - ] && [ -s "$path" ] && got=$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=codec_name,width,height -of csv=p=0 "$path")
    if [ "$got" = "${expect[$name]}" ]; then echo "PASS $kind $name: $got"
    else echo "FAIL $kind $name: got $got, expected ${expect[$name]}"; failed=$((failed + 1)); fi
    [ "$path" != - ] && rm -f "$path"
    checks=$((checks + 1))
done < "$fixture_dir/outputs"
[ "$checks" = 5 ] || { echo "FAIL: $checks of 5 results"; exit 1; }
[ "$failed" = 0 ] || { echo "Media preparation: $failed of $checks checks failed"; exit 1; }
echo "Media preparation: $checks checks passed"
