#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture_dir=$(mktemp -d /tmp/zuuned-artwork-print.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
read -r -a qt_flags <<< "$(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6Concurrent)"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -Isrc \
    tests/artwork-print/backend.cpp src/artwork/PrintRenderer.cpp \
    "${qt_flags[@]}" -o "$fixture_dir/artwork-print"
"$fixture_dir/artwork-print"
