#!/usr/bin/env bash
# Build both libFuzzer harnesses and run each for a few seconds, so a harness
# that stops linking fails the ordinary suite run (CI fuzzes them for longer).
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v "${FUZZ_CC:-clang}" >/dev/null; then
    echo "SKIP: fuzz smoke needs clang (set FUZZ_CC to another libFuzzer compiler)"
    exit 0
fi
bash tests/fuzz/run-fuzz.sh "${FUZZ_SMOKE_SECONDS:-3}"
