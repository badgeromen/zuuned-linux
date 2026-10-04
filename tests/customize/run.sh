#!/usr/bin/env bash
# Exercise production QML with a recording service; never opens the app or library.
set -euo pipefail

customize_test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
customize_repo_dir=$(cd -- "$customize_test_dir/../.." && pwd)
customize_test_root=$(mktemp -d /tmp/zuuned-customize-test.XXXXXX)
trap 'rm -rf -- "$customize_test_root"' EXIT

mkdir -p "$customize_test_root/imports/Zuuned" "$customize_test_root/runtime" \
    "$customize_test_root/config" "$customize_test_root/data" "$customize_test_root/cache"
chmod 700 "$customize_test_root/runtime"

for customize_component in CustomizeSheet EditTrackSheet CustomizeArtwork ArtworkImage CustomizeField Theme GradientText ZuneScrollBar SpinningDiscView VinylRecordView CassetteTile; do
    ln -s "$customize_repo_dir/qml/$customize_component.qml" \
        "$customize_test_root/imports/Zuuned/$customize_component.qml"
done
ln -s "$customize_repo_dir/qml/fonts" "$customize_test_root/imports/Zuuned/fonts"
cp "$customize_repo_dir/tests/artwork-print/stubs/ArtworkRequest.qml" "$customize_test_root/imports/Zuuned/ArtworkRequest.qml"

cat > "$customize_test_root/imports/Zuuned/qmldir" <<'MODULE'
module Zuuned
CustomizeSheet 1.0 CustomizeSheet.qml
EditTrackSheet 1.0 EditTrackSheet.qml
CustomizeArtwork 1.0 CustomizeArtwork.qml
ArtworkImage 1.0 ArtworkImage.qml
ArtworkRequest 1.0 ArtworkRequest.qml
CassetteTile 1.0 CassetteTile.qml
CustomizeField 1.0 CustomizeField.qml
GradientText 1.0 GradientText.qml
ZuneScrollBar 1.0 ZuneScrollBar.qml
SpinningDiscView 1.0 SpinningDiscView.qml
VinylRecordView 1.0 VinylRecordView.qml
singleton Prefs 1.0 Prefs.qml
singleton Theme 1.0 Theme.qml
singleton AppSettings 1.0 AppSettings.qml
singleton LibraryService 1.0 LibraryService.qml
MODULE

cat > "$customize_test_root/imports/Zuuned/AppSettings.qml" <<'SETTINGS'
pragma Singleton
import QtQuick
QtObject {
    readonly property string headerFont: "marker"
    readonly property string artworkStyle: "original"
    readonly property int artworkCleanDetail: 100
    readonly property int artworkHalftoneTexture: 14
    readonly property int artworkHalftoneDotSize: 17
    readonly property bool artworkHalftoneMonochrome: false
    readonly property int artworkWornTexture: 14
}
SETTINGS

cat > "$customize_test_root/imports/Zuuned/Prefs.qml" <<'PREFERENCES'
pragma Singleton
import QtQuick
QtObject { property string playerStyle: "disc" }
PREFERENCES

cat > "$customize_test_root/imports/Zuuned/LibraryService.qml" <<'SERVICE'
pragma Singleton
import QtQuick
QtObject {}
SERVICE

export QT_QPA_PLATFORM="${CUSTOMIZE_TEST_PLATFORM:-offscreen}" QT_QPA_PLATFORMTHEME='' QT_QUICK_CONTROLS_STYLE=Basic
export QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_TO_CONSOLE=1
if [[ ${CUSTOMIZE_TEST_RENDERER:-software} == opengl ]]; then
    export QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl
else
    export QT_QUICK_BACKEND=software
    unset QSG_RHI_BACKEND
fi
export XDG_RUNTIME_DIR="$customize_test_root/runtime" XDG_CONFIG_HOME="$customize_test_root/config"
export XDG_DATA_HOME="$customize_test_root/data" XDG_CACHE_HOME="$customize_test_root/cache"
customize_qml_runner=${QMLTESTRUNNER:-/usr/lib/qt6/bin/qmltestrunner}
if [[ ! -x "$customize_qml_runner" ]]; then
    customize_qml_runner=qmltestrunner6
fi
"$customize_qml_runner" -import "$customize_test_root/imports" \
    -input "${CUSTOMIZE_TEST_INPUT:-$customize_test_dir}" "$@"
