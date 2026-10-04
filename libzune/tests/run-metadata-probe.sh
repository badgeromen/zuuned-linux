#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-metadata-test.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT

common=(-nostdin -v error -y -f lavfi -i anullsrc=r=44100:cl=stereo -t 0.18 -map_metadata -1)
tags=(-metadata 'title=  Étude "Glass" \ mix  ' -metadata 'artist=  Featured  Artist  '
    -metadata 'album_artist=  Album Owner  ' -metadata 'album=  A  Record  '
    -metadata 'genre=  Alternative  ' -metadata 'track= 7/12 ' -metadata 'disc= 2/3 '
    -metadata 'date=2020-09-08')
for entry in mp3:libmp3lame flac:flac m4a:aac ogg:libvorbis opus:libopus; do
    extension=${entry%%:*}
    codec=${entry#*:}
    ffmpeg "${common[@]}" -c:a "$codec" "${tags[@]}" "$fixture_dir/tagged.$extension"
done
ffmpeg "${common[@]}" -c:a flac "$fixture_dir/missing.flac"
ffmpeg "${common[@]}" -c:a libvorbis -metadata $'title= \t\n ' -metadata $'artist=\u00a0\u3000' \
    -metadata 'album_artist=   ' -metadata 'album= ' -metadata 'genre=  ' \
    -metadata 'track= ' -metadata 'disc= ' -metadata 'date= ' "$fixture_dir/blank.ogg"
ffmpeg "${common[@]}" -c:a libopus -metadata title='Valid title' -metadata 'track=-1/12' \
    -metadata 'disc=99999999999999999999999' -metadata 'date=2020-99-99' "$fixture_dir/invalid.opus"
ffmpeg -nostdin -v error -y -f lavfi -i anullsrc=r=44100:cl=stereo \
    -f lavfi -i anullsrc=r=44100:cl=stereo -f lavfi -i color=c=blue:s=16x16:r=10 \
    -t 0.2 -map 0:a -map 1:a -map 2:v -c:a flac -c:v ffv1 -map_metadata -1 \
    -disposition:a:0 0 -disposition:a:1 default -metadata 'title= Container Title ' \
    -metadata 'album_artist= ' -metadata 'albumartist= Container Owner ' -metadata 'artist= ' \
    -metadata 'track=11/20' -metadata 'disc= ' -metadata 'date=2020-19' -metadata 'year=1998' \
    -metadata:s:a:0 'artist=Wrong Artist' -metadata:s:a:0 'genre=Wrong Genre' \
    -metadata:s:a:1 'title=Selected Title' -metadata:s:a:1 'artist= Selected Artist ' \
    -metadata:s:a:1 'album_artist=Wrong Stream Owner' -metadata:s:a:1 'album= Selected Album ' \
    -metadata:s:a:1 'genre= Selected Genre ' -metadata:s:a:1 'track=2' -metadata:s:a:1 'disc=3/4' \
    -metadata:s:a:1 'date=2024-01-01' -metadata:s:v:0 'artist=Wrong Video Artist' "$fixture_dir/selected.mkv"
ffmpeg -nostdin -v error -y -f lavfi -i anullsrc=r=44100:cl=stereo \
    -f lavfi -i anullsrc=r=44100:cl=stereo -t 0.2 -map 0:a -map 1:a -c:a flac -map_metadata -1 \
    -disposition:a:0 0 -disposition:a:1 0 -metadata:s:a:0 'artist= First Artist ' \
    -metadata:s:a:1 'artist=Wrong Second Artist' "$fixture_dir/first.mka"
ffmpeg -nostdin -v error -y -f lavfi -i color=c=blue:s=16x16:r=10 -t 0.2 -an -c:v ffv1 \
    -metadata 'title= Silent Video ' -metadata:s:v:0 'artist= Video Stream Artist ' "$fixture_dir/video-only.mkv"
ffmpeg "${common[@]}" -c:a flac -metadata $'title= first\nsecond\t"quote" \\ path $literal `tick` ' "$fixture_dir/escaped.flac"
cp "$fixture_dir/tagged.opus" "$fixture_dir/"'literal '\'' $(touch SHOULD_NOT_EXIST) `quotes`.opus'
printf 'malformed media\n' > "$fixture_dir/malformed.flac"
head -c 32 "$fixture_dir/tagged.ogg" > "$fixture_dir/truncated.ogg"

read -r -a av_flags <<< "$(pkg-config --cflags --libs libavformat libavutil)"
compile=(-std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Wpedantic -Wno-unused-function
    -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-omit-frame-pointer
    -Iinclude -Isrc -Wl,--gc-sections)
${CC:-cc} "${compile[@]}" -DUSE_LIBAV tests/metadata_probe.c "${av_flags[@]}" -o "$fixture_dir/probe-native"
${CC:-cc} "${compile[@]}" tests/metadata_probe.c -o "$fixture_dir/probe-fallback"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/probe-native" "$fixture_dir"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/probe-fallback" "$fixture_dir"

# Existing C++/Swift-through-C consumers rebuild against the enlarged struct;
# old fields retain their offsets, and current aggregate initialization works.
cat > "$fixture_dir/consumer.cpp" <<'EOF'
#include "zune.h"
#include <cstddef>
struct Previous { char *title, *artist, *albumartist, *album, *genre; uint16_t tracknumber; uint32_t duration_ms; };
static_assert(offsetof(Previous, title) == offsetof(ZuneMetadata, title));
static_assert(offsetof(Previous, artist) == offsetof(ZuneMetadata, artist));
static_assert(offsetof(Previous, albumartist) == offsetof(ZuneMetadata, albumartist));
static_assert(offsetof(Previous, album) == offsetof(ZuneMetadata, album));
static_assert(offsetof(Previous, genre) == offsetof(ZuneMetadata, genre));
static_assert(offsetof(Previous, tracknumber) == offsetof(ZuneMetadata, tracknumber));
static_assert(offsetof(Previous, duration_ms) == offsetof(ZuneMetadata, duration_ms));
static_assert(offsetof(ZuneMetadata, discnumber) >= sizeof(Previous));
static_assert(offsetof(ZuneMetadata, year) > offsetof(ZuneMetadata, discnumber));
int main() { ZuneMetadata metadata{}; metadata.discnumber = 2; metadata.year = 2020; return metadata.discnumber != 2; }
EOF
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -Iinclude "$fixture_dir/consumer.cpp" -o "$fixture_dir/consumer"
"$fixture_dir/consumer"
printf 'PASS rebuilt C++ public-header consumer and existing field offsets\n'
