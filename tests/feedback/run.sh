#!/usr/bin/env bash
# Production ToastHost in an isolated Qt Quick window; no app/services/device.
set -euo pipefail
feedback_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
feedback_repo=$(cd -- "$feedback_dir/../.." && pwd)
feedback_root=$(mktemp -d /tmp/zuuned-feedback-test.XXXXXX)
trap 'rm -rf -- "$feedback_root"' EXIT
mkdir -p "$feedback_root/imports/Zuuned" "$feedback_root/runtime" "$feedback_root/config" "$feedback_root/data" "$feedback_root/cache"
chmod 700 "$feedback_root/runtime"
for feedback_component in ToastHost Theme; do
    ln -s "$feedback_repo/qml/$feedback_component.qml" "$feedback_root/imports/Zuuned/$feedback_component.qml"
done
ln -s "$feedback_repo/qml/fonts" "$feedback_root/imports/Zuuned/fonts"
cat > "$feedback_root/imports/Zuuned/qmldir" <<'MODULE'
module Zuuned
ToastHost 1.0 ToastHost.qml
singleton Theme 1.0 Theme.qml
singleton AppSettings 1.0 AppSettings.qml
MODULE
cat > "$feedback_root/imports/Zuuned/AppSettings.qml" <<'SETTINGS'
pragma Singleton
import QtQuick
QtObject { readonly property string headerFont: "marker" }
SETTINGS
export QT_QPA_PLATFORM="${FEEDBACK_TEST_PLATFORM:-offscreen}" QT_QPA_PLATFORMTHEME='' QT_QUICK_CONTROLS_STYLE=Basic
export QT_QUICK_BACKEND=software QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_TO_CONSOLE=1
export XDG_RUNTIME_DIR="$feedback_root/runtime" XDG_CONFIG_HOME="$feedback_root/config" XDG_DATA_HOME="$feedback_root/data" XDG_CACHE_HOME="$feedback_root/cache"
/usr/lib/qt6/bin/qmltestrunner -import "$feedback_root/imports" -input "${FEEDBACK_TEST_INPUT:-$feedback_dir/tst_ToastHost.qml}" "$@"
