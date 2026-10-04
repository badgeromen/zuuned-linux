#!/usr/bin/env bash
set -euo pipefail
identity_test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
identity_test_root="$(mktemp -d /tmp/zuuned-music-migration-XXXXXX)"
trap 'rm -rf -- "$identity_test_root"' EXIT
read -r -a identity_qt_flags <<< "$(pkg-config --cflags --libs Qt6Core sqlite3)"
"${CXX:-c++}" -std=c++20 -fPIC -I"$identity_test_dir/../../src" \
    "$identity_test_dir/migration.cpp" "$identity_test_dir/../../src/library/LibraryDb.cpp" \
    -o "$identity_test_root/migration" "${identity_qt_flags[@]}"
"$identity_test_root/migration"
