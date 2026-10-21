#!/usr/bin/env bash
# T3.6: the cancel and disconnect flags cross threads, so every access goes
# through the atomic ZUNE_FLAG_GET/SET macros. A source check finds direct
# accesses (ThreadSanitizer ignores volatile ones), then ThreadSanitizer runs
# zune_abort against a polling thread. No USB.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-abort-flags.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
checks=0
direct=$(cat src/*.c src/hd/*.h tools/*.h \
    | sed -E 's/ZUNE_FLAG_(GET|SET)\([A-Za-z_0-9>-]*->(cancel_requested|disconnecting)//g; s/&[A-Za-z_0-9>.-]*->(cancel_requested|disconnecting)//g' \
    | grep -c -E -- '->(cancel_requested|disconnecting)\b' || true)
if [[ $direct != 0 ]]; then echo "FAIL: $direct direct flag accesses outside ZUNE_FLAG_GET/SET"; exit 1; fi
echo "PASS: every cancel/disconnect flag access uses ZUNE_FLAG_GET/SET"; checks=$((checks + 1))
if grep -n -E 'volatile int[^;]*(cancel_requested|disconnecting)|volatile int \*abort_flag' src/zune_internal.h src/ptp.h; then
    echo "FAIL: the flags are still volatile"; exit 1
fi
echo "PASS: the flags are plain ints read through atomics"; checks=$((checks + 1))
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections -fsanitize=thread -pthread \
    -Iinclude -Isrc $(pkg-config --cflags libusb-1.0) tests/abort-flags.c src/device.c src/wiretap.c \
    -Wl,--gc-sections -fsanitize=thread -o "$fixture_dir/abort-flags"
TSAN_OPTIONS=halt_on_error=1:exitcode=1 "$fixture_dir/abort-flags"
echo "Abort flag source checks: $checks checks, 0 failures"
