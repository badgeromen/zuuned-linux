#!/usr/bin/env bash
set -euo pipefail
import_test_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
import_test_build=$(mktemp -d /tmp/zuuned-device-music-import.XXXXXX)
trap 'rm -rf "$import_test_build"' EXIT
cmake -S "$import_test_source" -B "$import_test_build" -G Ninja
cmake --build "$import_test_build" -j 4
"$import_test_build/device-music-import"
"$import_test_build/device-album-art"
