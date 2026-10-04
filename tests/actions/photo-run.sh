#!/usr/bin/env bash
# Production QML with recording singletons; no app, media library, or USB.
set -euo pipefail
photo_actions_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
photo_repo_dir=$(cd -- "$photo_actions_dir/../.." && pwd)
photo_test_root=$(mktemp -d /tmp/zuuned-photo-actions.XXXXXX)
trap 'rm -rf -- "$photo_test_root"' EXIT
mkdir -p "$photo_test_root/imports/Zuuned" "$photo_test_root/runtime" "$photo_test_root/config" "$photo_test_root/data" "$photo_test_root/cache"
chmod 700 "$photo_test_root/runtime"
printf 'module Zuuned\n' > "$photo_test_root/imports/Zuuned/qmldir"
for photo_source in "$photo_repo_dir"/qml/*.qml; do
    photo_name=$(basename "$photo_source" .qml)
    if [[ -f "$photo_actions_dir/photo-stubs/$photo_name.qml" ]]; then continue; fi
    ln -s "$photo_source" "$photo_test_root/imports/Zuuned/$photo_name.qml"
    if rg -q '^pragma Singleton' "$photo_source"; then
        printf 'singleton %s 1.0 %s.qml\n' "$photo_name" "$photo_name" >> "$photo_test_root/imports/Zuuned/qmldir"
    else
        printf '%s 1.0 %s.qml\n' "$photo_name" "$photo_name" >> "$photo_test_root/imports/Zuuned/qmldir"
    fi
done
for photo_script in "$photo_repo_dir"/qml/*.js; do
    ln -s "$photo_script" "$photo_test_root/imports/Zuuned/$(basename "$photo_script")"
done
for photo_stub in "$photo_actions_dir"/photo-stubs/*.qml; do
    photo_name=$(basename "$photo_stub" .qml)
    cp "$photo_stub" "$photo_test_root/imports/Zuuned/$photo_name.qml"
    printf 'singleton %s 1.0 %s.qml\n' "$photo_name" "$photo_name" >> "$photo_test_root/imports/Zuuned/qmldir"
done
cp "$photo_repo_dir/tests/artwork-print/stubs/ArtworkRequest.qml" "$photo_test_root/imports/Zuuned/ArtworkRequest.qml"
printf 'ArtworkRequest 1.0 ArtworkRequest.qml\n' >> "$photo_test_root/imports/Zuuned/qmldir"
ln -s "$photo_repo_dir/qml/fonts" "$photo_test_root/imports/Zuuned/fonts"
ln -s "$photo_repo_dir/qml/images" "$photo_test_root/imports/Zuuned/images"
export QT_QPA_PLATFORM="${PHOTO_TEST_PLATFORM:-offscreen}" QT_QPA_PLATFORMTHEME='' QT_QUICK_CONTROLS_STYLE=Basic QT_QUICK_BACKEND="${PHOTO_TEST_BACKEND:-software}"
export XDG_RUNTIME_DIR="$photo_test_root/runtime" XDG_CONFIG_HOME="$photo_test_root/config" XDG_DATA_HOME="$photo_test_root/data" XDG_CACHE_HOME="$photo_test_root/cache"
/usr/lib/qt6/bin/qmltestrunner -import "$photo_test_root/imports" -input "${PHOTO_TEST_INPUT:-$photo_actions_dir/photo}" "$@"
