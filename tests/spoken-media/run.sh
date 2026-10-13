#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
spoken_build=$(mktemp -d /tmp/zuuned-spoken.XXXXXX)
trap 'rm -rf -- "$spoken_build"' EXIT
cat > "$spoken_build/chapters.txt" <<'META'
;FFMETADATA1
album=Test Book
artist=Test Author
title=Part One
[CHAPTER]
TIMEBASE=1/1000
START=0
END=1000
title=Opening
[CHAPTER]
TIMEBASE=1/1000
START=1000
END=2000
title=Next
META
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i 'sine=frequency=220:duration=2' \
    -i "$spoken_build/chapters.txt" -map_metadata 1 -map_chapters 1 -c:a aac -f ipod "$spoken_build/book.m4b"
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i 'sine=frequency=220:duration=1' \
    -c:a libmp3lame -metadata album='Test Book' -metadata track='12/20' -metadata disc='2/3' "$spoken_build/part.mp3"
printf '#EXTM3U\nhttps://example.test/book.mp3\n' > "$spoken_build/remote.m3u"
read -r -a spoken_flags <<< "$(pkg-config --cflags --libs Qt6Core libavformat libavcodec libavutil)"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -fsanitize=address,undefined \
    -Isrc tests/spoken-media/backend.cpp src/spoken/PodcastFeed.cpp src/spoken/AudiobookProbe.cpp \
    "${spoken_flags[@]}" -o "$spoken_build/test"
ASAN_OPTIONS=detect_leaks=1 "$spoken_build/test" "$spoken_build/book.m4b" "$spoken_build/part.mp3" "$spoken_build/remote.m3u"
