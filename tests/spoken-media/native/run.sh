#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
spoken_native_root=$(mktemp -d /tmp/zuuned-spoken-native-XXXXXX)
printf '%s\n' "$spoken_native_root" > /tmp/zuuned-spoken-native-last.txt
mkdir -p "$spoken_native_root"/{data,cache,config,runtime}
chmod 700 "$spoken_native_root/runtime"
cat > "$spoken_native_root/chapters.txt" <<'META'
;FFMETADATA1
album=Fixture Book
artist=Fixture Author
title=Part One
[CHAPTER]
TIMEBASE=1/1000
START=500
END=3500
title=Opening
[CHAPTER]
TIMEBASE=1/1000
START=4000
END=7500
title=Next
META
ffmpeg -nostdin -v error -f lavfi -i 'sine=frequency=220:duration=8' -i "$spoken_native_root/chapters.txt" -map_metadata 1 -map_chapters 1 -c:a aac -f ipod "$spoken_native_root/book.m4b"
ffmpeg -nostdin -v error -f lavfi -i 'sine=frequency=220:duration=8' -c:a libmp3lame "$spoken_native_root/part.mp3"
# Show notes repeated on every item compress about 300:1, past Qt's own bomb
# check (10 MiB at a high ratio) while well inside the feed limit.
{
    printf '<rss version="2.0"><channel><title>Repeated notes</title>'
    notes=$(printf 'n%.0s' {1..4096})
    for i in $(seq 1 3000); do
        printf '<item><guid>r%d</guid><title>Episode %d</title><description>%s</description><enclosure url="https://example.test/r%d.mp3" type="audio/mpeg"/></item>' "$i" "$i" "$notes" "$i"
    done
    printf '</channel></rss>'
} | gzip -9 > "$spoken_native_root/ratio.xml.gz"
cmake -S tests/spoken-media/native -B /tmp/zuuned-spoken-native-build -G Ninja >/tmp/zuuned-spoken-native-config.log 2>&1
cmake --build /tmp/zuuned-spoken-native-build -j4 >/tmp/zuuned-spoken-native-build.log 2>&1
export SPOKEN_GATE_ROOT="$spoken_native_root" ZUUNED_MPV_AO=null
export XDG_DATA_HOME="$spoken_native_root/data" XDG_CONFIG_HOME="$spoken_native_root/config" XDG_CACHE_HOME="$spoken_native_root/cache" XDG_RUNTIME_DIR="$spoken_native_root/runtime"
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME=''
/tmp/zuuned-spoken-native-build/spoken-native
