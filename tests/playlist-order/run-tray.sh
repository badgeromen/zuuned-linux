#!/usr/bin/env bash
set -euo pipefail
playlist_test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
playlist_repo_dir=$(cd -- "$playlist_test_dir/../.." && pwd)
playlist_test_root=$(mktemp -d /tmp/zuuned-playlist-order-qml.XXXXXX)
trap 'rm -rf -- "$playlist_test_root"' EXIT
mkdir -p "$playlist_test_root/imports/Zuuned" "$playlist_test_root/runtime" \
    "$playlist_test_root/config" "$playlist_test_root/data" "$playlist_test_root/cache"
chmod 700 "$playlist_test_root/runtime"
ln -s "$playlist_repo_dir/qml/TrayState.qml" "$playlist_test_root/imports/Zuuned/TrayState.qml"
ln -s "$playlist_test_dir/LibraryService.qml" "$playlist_test_root/imports/Zuuned/LibraryService.qml"
cat > "$playlist_test_root/imports/Zuuned/qmldir" <<'MODULE'
module Zuuned
singleton TrayState 1.0 TrayState.qml
singleton LibraryService 1.0 LibraryService.qml
MODULE
export QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME='' QT_QUICK_BACKEND=software
export XDG_RUNTIME_DIR="$playlist_test_root/runtime" XDG_CONFIG_HOME="$playlist_test_root/config"
export XDG_DATA_HOME="$playlist_test_root/data" XDG_CACHE_HOME="$playlist_test_root/cache"
playlist_runner=${QMLTESTRUNNER:-/usr/lib/qt6/bin/qmltestrunner}
if [[ ! -x "$playlist_runner" ]]; then playlist_runner=qmltestrunner; fi
"$playlist_runner" -import "$playlist_test_root/imports" -input "$playlist_test_dir/tst_tray.qml" "$@"
