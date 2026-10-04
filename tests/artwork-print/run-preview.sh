#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture_dir=$(mktemp -d /tmp/zuuned-artwork-preview-test.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
read -r -a qt_flags <<< "$(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6Qml Qt6Concurrent Qt6Sql)"
read -r -a qt_cflags <<< "$(pkg-config --cflags Qt6Core Qt6Gui Qt6Qml Qt6Concurrent Qt6Sql)"
qt_libexec=$(pkg-config --variable=libexecdir Qt6Core)
"$qt_libexec/moc" "${qt_cflags[@]}" tools/artwork-preview/PreviewController.h -o "$fixture_dir/moc_preview.cpp"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -Isrc -Itools/artwork-preview \
    tests/artwork-print/preview.cpp tools/artwork-preview/PreviewController.cpp \
    src/artwork/PrintRenderer.cpp "$fixture_dir/moc_preview.cpp" \
    "${qt_flags[@]}" -o "$fixture_dir/artwork-preview"
XDG_CACHE_HOME="$fixture_dir/cache" XDG_CONFIG_HOME="$fixture_dir/config" \
    XDG_DATA_HOME="$fixture_dir/data" "$fixture_dir/artwork-preview"
