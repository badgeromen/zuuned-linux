#!/usr/bin/env bash
# T3.7: split functions and consolidated helpers keep their behavior. Offline,
# under ASan + UBSan (leak checks on).
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-shrink-giants.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
flags=(-std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter
       -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
       -Iinclude -Isrc -DUSE_LIBAV $(pkg-config --cflags libusb-1.0 libavformat libavutil))
srcs=$(ls src/*.c | grep -v driverkit_usb.c)
${CC:-cc} "${flags[@]}" -Wno-error tests/shrink-giants.c $srcs -lavcodec \
    $(pkg-config --libs libusb-1.0 libavformat libavutil) -lgcrypt -lm -pthread \
    -o "$fixture_dir/shrink-giants" 2> "$fixture_dir/build.log" || { cat "$fixture_dir/build.log"; exit 1; }
if grep -v '^src/mtpz.c' "$fixture_dir/build.log" | grep -q 'warning:'; then
    cat "$fixture_dir/build.log"; echo "FAIL: warnings outside mtpz.c"; exit 1
fi
"$fixture_dir/shrink-giants"
${CC:-cc} "${flags[@]}" tests/mtp-helpers.c -o "$fixture_dir/mtp-helpers"
"$fixture_dir/mtp-helpers"
