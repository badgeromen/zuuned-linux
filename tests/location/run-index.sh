#!/usr/bin/env bash
set -euo pipefail
location_test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
location_test_root="$(mktemp -d /tmp/zuuned-location-index-XXXXXX)"
trap 'rm -rf -- "$location_test_root"' EXIT
read -r -a location_qt_flags <<< "$(pkg-config --cflags --libs Qt6Core)"
"${CXX:-c++}" -std=c++20 -I"$location_test_dir/../../src" \
    "$location_test_dir/index.cpp" -o "$location_test_root/index" "${location_qt_flags[@]}"
"$location_test_root/index"
