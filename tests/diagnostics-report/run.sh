#!/usr/bin/env bash
set -euo pipefail
report_test_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
report_test_build=$(mktemp -d /tmp/zuuned-report-tests.XXXXXX)
printf 'Build evidence: %s/build.log\n' "$report_test_build"
cmake -S "$report_test_source" -B "$report_test_build" -G Ninja > "$report_test_build/build.log" 2>&1
cmake --build "$report_test_build" -j 4 >> "$report_test_build/build.log" 2>&1
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME='' "$report_test_build/diagnostics-report"
