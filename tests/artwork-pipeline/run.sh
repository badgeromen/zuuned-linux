#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture_dir=$(mktemp -d /tmp/zuuned-artwork-pipeline-test.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
read -r -a qt_flags <<< "$(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6Qml Qt6Concurrent)"
read -r -a qt_cflags <<< "$(pkg-config --cflags Qt6Core Qt6Gui Qt6Qml Qt6Concurrent)"
qt_libexec=$(pkg-config --variable=libexecdir Qt6Core)
"$qt_libexec/moc" "${qt_cflags[@]}" src/artwork/ArtworkRequest.h -o "$fixture_dir/moc_request.cpp"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -Isrc \
    tests/artwork-pipeline/backend.cpp src/artwork/ArtworkPipeline.cpp \
    src/artwork/ArtworkRequest.cpp src/artwork/PrintRenderer.cpp \
    "$fixture_dir/moc_request.cpp" "${qt_flags[@]}" -o "$fixture_dir/artwork-pipeline"
XDG_CACHE_HOME="$fixture_dir/cache" XDG_CONFIG_HOME="$fixture_dir/config" \
    XDG_DATA_HOME="$fixture_dir/data" "$fixture_dir/artwork-pipeline"
