#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture_dir=$(mktemp -d /tmp/zuuned-artwork-persistence.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
read -r -a qt_flags <<< "$(pkg-config --cflags --libs Qt6Core sqlite3)"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -Isrc \
    tests/artwork-persistence/backend.cpp src/library/LibraryDb.cpp \
    "${qt_flags[@]}" -o "$fixture_dir/test"
"$fixture_dir/test"
