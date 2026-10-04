#!/usr/bin/env bash
set -euo pipefail
playlist_test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
playlist_repo_dir=$(cd -- "$playlist_test_dir/../.." && pwd)
playlist_test_root=$(mktemp -d /tmp/zuuned-playlist-order-build.XXXXXX)
trap 'rm -rf -- "$playlist_test_root"' EXIT
read -r -a playlist_qt_flags <<< "$(pkg-config --cflags --libs Qt6Core sqlite3)"
c++ -std=c++20 -fPIC -I"$playlist_repo_dir/src" \
    "$playlist_test_dir/db.cpp" "$playlist_repo_dir/src/library/LibraryDb.cpp" \
    "${playlist_qt_flags[@]}" -o "$playlist_test_root/test"
"$playlist_test_root/test"
