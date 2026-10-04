#!/usr/bin/env bash
# Actual AppSettings + production Appearance QML in an isolated test window.
set -euo pipefail
cd "$(dirname "$0")/../.."
settings_repo=$PWD
settings_root=$(mktemp -d /tmp/zuuned-artwork-settings.XXXXXX)
trap 'rm -rf -- "$settings_root"' EXIT
mkdir -p "$settings_root/imports/Zuuned" "$settings_root/runtime" "$settings_root/config" \
    "$settings_root/data" "$settings_root/cache"
chmod 700 "$settings_root/runtime"
export XDG_CONFIG_HOME="$settings_root/config" XDG_DATA_HOME="$settings_root/data"
export XDG_CACHE_HOME="$settings_root/cache" XDG_RUNTIME_DIR="$settings_root/runtime"
export QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME='' QT_QUICK_BACKEND=software
export QT_QUICK_CONTROLS_STYLE=Basic QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_TO_CONSOLE=1
read -r -a settings_flags <<< "$(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6Qml Qt6QuickTest)"
read -r -a settings_cflags <<< "$(pkg-config --cflags Qt6Core Qt6Gui Qt6Qml Qt6QuickTest)"
settings_libexec=$(pkg-config --variable=libexecdir Qt6Core)
"$settings_libexec/moc" "${settings_cflags[@]}" src/AppSettings.h -o "$settings_root/moc_AppSettings.cpp"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -Isrc \
    tests/artwork-print/settings.cpp "$settings_root/moc_AppSettings.cpp" \
    "${settings_flags[@]}" -o "$settings_root/settings-test"
"$settings_root/settings-test"
"$settings_root/settings-test" --restore

for settings_component in ArtworkStyleSettings Theme; do
    ln -s "$settings_repo/qml/$settings_component.qml" "$settings_root/imports/Zuuned/$settings_component.qml"
done
ln -s "$settings_repo/qml/fonts" "$settings_root/imports/Zuuned/fonts"
cat > "$settings_root/imports/Zuuned/qmldir" <<'MODULE'
module Zuuned
ArtworkStyleSettings 1.0 ArtworkStyleSettings.qml
ArtworkImage 1.0 ArtworkImage.qml
singleton Theme 1.0 Theme.qml
MODULE
# Rendering is exercised separately by the production pipeline tests. Here the
# Image stand-in isolates settings interactions and verifies the paired source.
cat > "$settings_root/imports/Zuuned/ArtworkImage.qml" <<'IMAGE'
import QtQuick
Image {}
IMAGE
"$settings_libexec/moc" "${settings_cflags[@]}" tests/artwork-print/settings-quick.cpp -o "$settings_root/settings-quick.moc"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -Isrc -I"$settings_root" \
    tests/artwork-print/settings-quick.cpp "$settings_root/moc_AppSettings.cpp" \
    "${settings_flags[@]}" -o "$settings_root/settings-quick"
"$settings_root/settings-quick" -import "$settings_root/imports" \
    -input tests/artwork-print/tst_ArtworkSettings.qml "$@"
