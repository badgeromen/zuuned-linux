#!/usr/bin/env bash
set -euo pipefail
identity_test_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
identity_test_build=$(mktemp -d /tmp/zuuned-application-identity.XXXXXX)
trap 'rm -rf "$identity_test_build"' EXIT
cmake -S "$identity_test_source" -B "$identity_test_build" -G Ninja
cmake --build "$identity_test_build" -j 4
QT_QPA_PLATFORM=offscreen "$identity_test_build/application-identity"
