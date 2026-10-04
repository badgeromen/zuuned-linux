#!/usr/bin/env bash
set -euo pipefail
identity_test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
identity_test_root="$(mktemp -d /tmp/zuuned-music-identity-XXXXXX)"
trap 'rm -rf -- "$identity_test_root"' EXIT
read -r -a identity_qt_flags <<< "$(pkg-config --cflags --libs Qt6Core)"
"${CXX:-c++}" -std=c++20 -fPIC -I"$identity_test_dir/../../src" \
    "$identity_test_dir/identity.cpp" -o "$identity_test_root/identity" "${identity_qt_flags[@]}"
"$identity_test_root/identity"
