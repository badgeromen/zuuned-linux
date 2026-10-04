#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
music_build=$(mktemp -d /tmp/zuuned-music-import-build.XXXXXX)
trap 'rm -rf -- "$music_build"' EXIT
mkdir -p "$music_build/config" "$music_build/cache" "$music_build/data"
export XDG_CONFIG_HOME="$music_build/config" XDG_CACHE_HOME="$music_build/cache" XDG_DATA_HOME="$music_build/data"
export QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_TO_CONSOLE=1
read -r -a music_qt <<< "$(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6Qml Qt6Concurrent sqlite3 libavformat libavcodec libavutil)"
read -r -a music_cflags <<< "$(pkg-config --cflags Qt6Core Qt6Gui Qt6Qml Qt6Concurrent libavformat libavcodec libavutil)"
music_libexec=$(pkg-config --variable=libexecdir Qt6Core)
for music_type in library/LibraryScanner library/LocalTrackModel sync/SyncQueueModel; do
    "$music_libexec/moc" "${music_cflags[@]}" "src/$music_type.h" -o "$music_build/moc_${music_type##*/}.cpp"
done
${CC:-cc} -std=gnu99 -O1 -g -DUSE_LIBAV -ffunction-sections -fdata-sections \
    -Ilibzune/include -Ilibzune/src "${music_cflags[@]}" -c libzune/src/util.c -o "$music_build/util.o"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -Isrc -Ilibzune/include \
    tests/music-import/backend.cpp src/library/LibraryScanner.cpp src/library/LibraryDb.cpp \
    src/library/VideoNaming.cpp src/library/LocalTrackModel.cpp src/sync/SyncQueueModel.cpp \
    "$music_build/moc_LibraryScanner.cpp" "$music_build/moc_LocalTrackModel.cpp" \
    "$music_build/moc_SyncQueueModel.cpp" "$music_build/util.o" \
    -Wl,--gc-sections "${music_qt[@]}" -o "$music_build/test"
"$music_build/test"
