#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
copy_build=$(mktemp -d /tmp/zuuned-local-copies-build.XXXXXX)
trap 'rm -rf -- "$copy_build"' EXIT
read -r -a copy_qt <<< "$(pkg-config --cflags --libs Qt6Core Qt6Qml sqlite3)"
read -r -a copy_cflags <<< "$(pkg-config --cflags Qt6Core Qt6Qml)"
copy_libexec=$(pkg-config --variable=libexecdir Qt6Core)
"$copy_libexec/moc" "${copy_cflags[@]}" src/library/LocalTrackModel.h -o "$copy_build/moc_LocalTrackModel.cpp"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -Isrc \
    tests/local-music-copies/backend.cpp src/library/LibraryDb.cpp src/library/LocalTrackModel.cpp \
    "$copy_build/moc_LocalTrackModel.cpp" "${copy_qt[@]}" -o "$copy_build/test"
"$copy_build/test"
