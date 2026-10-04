#!/usr/bin/env bash
# Structural bundle gate only: does not start the app, call providers or USB.
set -euo pipefail
zuuned_appdir=$(realpath -e "${1:?usage: verify-appdir.sh AppDir}")
[[ -d "$zuuned_appdir/usr" ]] || { echo "Not an AppDir: $zuuned_appdir" >&2; exit 1; }
require_file() {
    [[ -s "$zuuned_appdir/$1" ]] || { echo "Missing bundle file: $1" >&2; exit 1; }
    [[ $(realpath -e -- "$zuuned_appdir/$1") == "$zuuned_appdir/"* ]] \
        || { echo "Bundle file escapes the AppDir: $1" >&2; exit 1; }
}
require_library() {
    local candidate
    while IFS= read -r -d '' candidate; do
        if [[ -f "$candidate" && $(realpath -e "$candidate") == "$zuuned_appdir/"* ]] \
            && LC_ALL=C readelf --file-header "$candidate" >/dev/null 2>&1; then return; fi
    done < <(find "$zuuned_appdir/usr/lib" -maxdepth 1 -name "$1" -print0)
    echo "Missing bundled library: $1" >&2
    exit 1
}
for executable in zuuned ffmpeg ffprobe; do
    require_file "usr/bin/$executable"
    [[ -x "$zuuned_appdir/usr/bin/$executable" ]] || { echo "Not executable: $executable" >&2; exit 1; }
    LC_ALL=C readelf --file-header "$zuuned_appdir/usr/bin/$executable" >/dev/null 2>&1 \
        || { echo "Not a native ELF executable: $executable" >&2; exit 1; }
done
for module in QtQml QtQml/Models QtQml/WorkerScript QtQuick QtQuick/Window QtQuick/Layouts \
              QtQuick/Templates QtQuick/Controls QtQuick/Controls/Basic QtQuick/Dialogs \
              Qt5Compat/GraphicalEffects Qt5Compat/GraphicalEffects/private; do
    require_file "usr/qml/$module/qmldir"
done
for library in libQt6Core.so.6 libQt6Quick.so.6 libQt6Network.so.6 libQt6QuickControls2.so.6 \
               libQt6QuickDialogs2.so.6 'libmpv.so.*' 'libavcodec.so.*' 'libavformat.so.*' \
               'libavutil.so.*' 'libswscale.so.*' 'libswresample.so.*' 'libmp3lame.so.*' \
               'libusb-1.0.so.*' 'libgcrypt.so.*' 'libsqlite3.so.*' \
               libssl.so.3 libcrypto.so.3; do
    require_library "$library"
done
# Qt's OpenSSL backend dlopens these two libraries; DT_NEEDED alone cannot
# discover them. Keep the explicit check even when media libraries pull them in.
for item in usr/plugins/platforms/libqxcb.so usr/plugins/imageformats/libqjpeg.so \
            usr/plugins/platformthemes/libqxdgdesktopportal.so \
            usr/plugins/tls/libqopensslbackend.so \
            usr/qml/Qt5Compat/GraphicalEffects/libqtgraphicaleffectsplugin.so \
            usr/qml/Qt5Compat/GraphicalEffects/private/libqtgraphicaleffectsprivateplugin.so \
            usr/lib/udev/rules.d/68-zuuned.rules usr/lib/udev/rules.d/72-zuuned.rules \
            usr/share/zuuned/zuuned-build.json AppRun; do
    require_file "$item"
done
cmp --silent "$zuuned_appdir/usr/lib/udev/rules.d/68-zuuned.rules" \
    "$zuuned_appdir/usr/lib/udev/rules.d/72-zuuned.rules" \
    || { echo "The two ordered USB rules must use identical canonical bytes" >&2; exit 1; }
[[ ! -L "$zuuned_appdir/AppRun" ]] || { echo "AppRun must set the bundled FFmpeg PATH" >&2; exit 1; }
bash "$(dirname -- "$0")/verify-elf-dependencies.sh" "$zuuned_appdir"
echo "Bundle inventory passed: native Qt/QML, desktop portal, media libraries, FFmpeg tools, TLS, xcb and USB rule."
echo "This does not certify host ABI/graphics/audio compatibility; run the clean-runtime gate."
