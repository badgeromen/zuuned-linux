#!/usr/bin/env bash
set -euo pipefail
photo_test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
photo_repo_dir=$(cd -- "$photo_test_dir/../.." && pwd)
photo_test_root=$(mktemp -d /tmp/zuuned-photo-albums-build.XXXXXX)
trap 'rm -rf -- "$photo_test_root"' EXIT
read -r -a photo_qt_flags <<< "$(pkg-config --cflags --libs Qt6Core sqlite3)"
c++ -std=c++20 -fPIC -I"$photo_repo_dir/src" \
    "$photo_test_dir/db.cpp" "$photo_repo_dir/src/library/LibraryDb.cpp" \
    "${photo_qt_flags[@]}" -o "$photo_test_root/test"
"$photo_test_root/test"
