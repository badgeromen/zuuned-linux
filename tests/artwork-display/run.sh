#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
artwork_display_root=$(mktemp -d /tmp/zuuned-artwork-display.XXXXXX)
trap 'rm -rf "$artwork_display_root"' EXIT
mkdir -p "$artwork_display_root/runtime"
chmod 700 "$artwork_display_root/runtime"
cmake -S tests/artwork-display -B build/artwork-display-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/artwork-display-tests -j 4
export XDG_CONFIG_HOME="$artwork_display_root/config" XDG_CACHE_HOME="$artwork_display_root/cache"
export XDG_DATA_HOME="$artwork_display_root/data" XDG_RUNTIME_DIR="$artwork_display_root/runtime"
export QT_QPA_PLATFORM="${ARTWORK_TEST_PLATFORM:-offscreen}" QT_QPA_PLATFORMTHEME=''
export QT_QUICK_CONTROLS_STYLE=Basic
export QT_QUICK_BACKEND="${ARTWORK_TEST_RENDERER:-software}"
if [[ "$QT_QUICK_BACKEND" == rhi ]]; then export QSG_RHI_BACKEND=opengl; fi
build/artwork-display-tests/artwork-display -input "${ARTWORK_TEST_INPUT:-tests/artwork-display/qml}" "$@"
