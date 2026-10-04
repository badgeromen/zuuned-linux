#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
zuuned_udev_tmp=$(mktemp -d /tmp/zuuned-udev-test.XXXXXX)
trap 'rm -rf "$zuuned_udev_tmp"' EXIT
printf '<RCC><qresource prefix="/zuuned/packaging"><file alias="68-zuuned.rules">%s/packaging/68-zuuned.rules</file></qresource></RCC>\n' "$PWD" > "$zuuned_udev_tmp/rules.qrc"
"$(pkg-config --variable=libexecdir Qt6Core)/rcc" "$zuuned_udev_tmp/rules.qrc" -o "$zuuned_udev_tmp/resource.cpp"
c++ -std=c++20 -fPIC -Isrc tests/udev-setup/backend.cpp "$zuuned_udev_tmp/resource.cpp" \
    $(pkg-config --cflags --libs Qt6Core) -o "$zuuned_udev_tmp/test"
"$zuuned_udev_tmp/test"
udevadm verify packaging/68-zuuned.rules
