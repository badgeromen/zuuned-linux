#!/usr/bin/env bash
# libFuzzer runs for the ZMDB and MTP reply parsers (clang required).
# Usage: tests/fuzz/run-fuzz.sh [seconds-per-fuzzer]   (default 60)
set -euo pipefail
cd "$(dirname "$0")/../.."
seconds=${1:-60}
work=$(mktemp -d "${TMPDIR:-/tmp}/libzune-fuzz.XXXXXX")
trap 'rm -rf "$work"' EXIT
python3 tests/fuzz/make-seeds.py "$work/seeds"
flags=(-std=c99 -D_GNU_SOURCE -O1 -g -fsanitize=fuzzer,address,undefined
    -fno-sanitize-recover=undefined -ffunction-sections -fdata-sections -pthread
    -Iinclude -Isrc -Wl,--gc-sections)
${FUZZ_CC:-clang} "${flags[@]}" tests/fuzz/fuzz_zmdb.c src/podcast_state.c src/wiretap.c \
    -o "$work/fuzz_zmdb"
${FUZZ_CC:-clang} "${flags[@]}" tests/fuzz/fuzz_mtp_unpack.c src/mtp.c src/devinfo.c src/wiretap.c \
    tests/ptp_file_adapter.c \
    -o "$work/fuzz_mtp_unpack"
export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
for fuzzer in zmdb mtp_unpack; do
    corpus="$work/corpus-$fuzzer"
    mkdir -p "$corpus"
    seeds="$work/seeds/${fuzzer%%_*}"
    "$work/fuzz_$fuzzer" -max_total_time="$seconds" -timeout=10 -rss_limit_mb=2048 \
        -max_len=65536 -print_final_stats=1 -artifact_prefix="$work/" \
        "$corpus" "$seeds" 2> "$work/$fuzzer.log" || {
            tail -40 "$work/$fuzzer.log" >&2
            echo "FAIL: fuzz_$fuzzer found a crash" >&2
            exit 1
        }
    printf 'PASS: fuzz_%s ran %ss: %s\n' "$fuzzer" "$seconds" \
        "$(grep -m1 -E 'stat::number_of_executed_units' "$work/$fuzzer.log" | awk '{print $2 " executions"}')"
done
