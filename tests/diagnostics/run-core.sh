#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
zuuned_diagnostics_tmp=$(mktemp -d /tmp/zuuned-diagnostics-core.XXXXXX)
trap 'rm -rf "$zuuned_diagnostics_tmp"' EXIT
c++ -std=c++20 -fPIC -pthread -Wall -Wextra -Werror -Isrc \
    tests/diagnostics/core.cpp src/diagnostics/SessionLog.cpp src/diagnostics/Redactor.cpp \
    $(pkg-config --cflags --libs Qt6Core) -o "$zuuned_diagnostics_tmp/core"
"$zuuned_diagnostics_tmp/core"
