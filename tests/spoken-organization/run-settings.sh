#!/usr/bin/env bash
# Actual persistent settings in isolated storage; no GUI, media, or USB.
set -euo pipefail
spoken_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
spoken_settings_root=$(mktemp -d /tmp/zuuned-spoken-settings.XXXXXX)
trap 'rm -rf -- "$spoken_settings_root"' EXIT
export XDG_CONFIG_HOME="$spoken_settings_root/config"
mkdir -p "$XDG_CONFIG_HOME"
read -r -a spoken_flags <<< "$(pkg-config --cflags --libs Qt6Core Qt6Qml)"
read -r -a spoken_cflags <<< "$(pkg-config --cflags Qt6Core Qt6Qml)"
spoken_libexec=$(pkg-config --variable=libexecdir Qt6Core)
"$spoken_libexec/moc" "${spoken_cflags[@]}" "$spoken_repo/src/AppSettings.h" -o "$spoken_settings_root/moc_AppSettings.cpp"
"${CXX:-c++}" -std=c++20 -O1 -Wall -Wextra -fPIC -I"$spoken_repo/src" \
    "$spoken_repo/tests/spoken-organization/settings.cpp" "$spoken_settings_root/moc_AppSettings.cpp" \
    "${spoken_flags[@]}" -o "$spoken_settings_root/settings-test"
"$spoken_settings_root/settings-test"
"$spoken_settings_root/settings-test" --restore
