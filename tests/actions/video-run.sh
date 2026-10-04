#!/usr/bin/env bash
# Real video views against recording services: no application DB or hardware.
set -euo pipefail
video_repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
video_test_root=$(mktemp -d /tmp/zuuned-video-actions.XXXXXX)
trap 'rm -rf -- "$video_test_root"' EXIT
mkdir -p "$video_test_root/imports/Zuuned" "$video_test_root/runtime" "$video_test_root/config" "$video_test_root/data" "$video_test_root/cache"
chmod 700 "$video_test_root/runtime"
printf 'module Zuuned\n' > "$video_test_root/imports/Zuuned/qmldir"
for video_component in "$video_repo_dir"/qml/*.qml; do
    video_name=$(basename "$video_component" .qml)
    ln -s "$video_component" "$video_test_root/imports/Zuuned/$video_name.qml"
    if rg -q '^pragma Singleton' "$video_component"; then
        printf 'singleton %s 1.0 %s.qml\n' "$video_name" "$video_name" >> "$video_test_root/imports/Zuuned/qmldir"
    else
        printf '%s 1.0 %s.qml\n' "$video_name" "$video_name" >> "$video_test_root/imports/Zuuned/qmldir"
    fi
done
ln -s "$video_repo_dir/qml/fonts" "$video_test_root/imports/Zuuned/fonts"
ln -s "$video_repo_dir/qml/images" "$video_test_root/imports/Zuuned/images"
for video_stub in "$video_repo_dir"/tests/actions/video/stubs/*.qml; do
    video_name=$(basename "$video_stub" .qml)
    # Replace the symlink, never its production target.
    cp --remove-destination "$video_stub" "$video_test_root/imports/Zuuned/$video_name.qml"
    if ! rg -q "^((singleton )?$video_name) " "$video_test_root/imports/Zuuned/qmldir"; then
        if rg -q '^pragma Singleton' "$video_stub"; then
            printf 'singleton %s 1.0 %s.qml\n' "$video_name" "$video_name" >> "$video_test_root/imports/Zuuned/qmldir"
        else
            printf '%s 1.0 %s.qml\n' "$video_name" "$video_name" >> "$video_test_root/imports/Zuuned/qmldir"
        fi
    fi
done
cp "$video_repo_dir/tests/artwork-print/stubs/ArtworkRequest.qml" "$video_test_root/imports/Zuuned/ArtworkRequest.qml"
printf 'ArtworkRequest 1.0 ArtworkRequest.qml\n' >> "$video_test_root/imports/Zuuned/qmldir"
export QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME='' QT_QUICK_CONTROLS_STYLE=Basic QT_QUICK_BACKEND=software
export XDG_RUNTIME_DIR="$video_test_root/runtime" XDG_CONFIG_HOME="$video_test_root/config" XDG_DATA_HOME="$video_test_root/data" XDG_CACHE_HOME="$video_test_root/cache"
/usr/lib/qt6/bin/qmltestrunner -import "$video_test_root/imports" -input "$video_repo_dir/tests/actions/video/tst_VideoActions.qml" "$@"
