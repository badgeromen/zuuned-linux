#!/usr/bin/env bash
# Send-retry triage: refusals are tried once and reported with their own
# code; transport failures keep three attempts. ptp_transaction is a fixture.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-send-retry.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
# Photo preparation runs ffmpeg first; this one passes the JPEG through.
cat > "$fixture_dir/ffmpeg" <<'TOOL'
#!/bin/sh
for last in "$@"; do :; done
cp "$3" "$last"
TOOL
chmod 755 "$fixture_dir/ffmpeg"
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/send-retry.c src/wiretap.c src/track.c src/upload.c tests/ptp_file_adapter.c src/photo.c src/video.c src/mtp.c \
    src/util.c src/thumbnail.c src/finalize.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/send-retry"
ZUNE_FFMPEG="$fixture_dir/ffmpeg" TMPDIR="$fixture_dir" \
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    "$fixture_dir/send-retry" "$fixture_dir"
