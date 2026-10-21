#!/usr/bin/env bash
# Photo upload: JPEG-only fallback and device album lookup before creation.
# ptp_transaction is an in-memory device; ffmpeg is a pass-through or a
# failing fake.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-photo-upload.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
mkdir "$fixture_dir/tmp"
cat > "$fixture_dir/ffmpeg-ok" <<'TOOL'
#!/bin/sh
for last in "$@"; do :; done
cp "$3" "$last"
TOOL
printf '#!/bin/sh\nexit 1\n' > "$fixture_dir/ffmpeg-fail"
chmod 755 "$fixture_dir/ffmpeg-ok" "$fixture_dir/ffmpeg-fail"
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/photo-upload.c src/wiretap.c src/photo.c src/upload.c tests/ptp_file_adapter.c src/mtp.c src/util.c \
    -Wl,--gc-sections -fsanitize=address,undefined -o "$fixture_dir/photo-upload"
TMPDIR="$fixture_dir/tmp" \
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    "$fixture_dir/photo-upload" "$fixture_dir" "$fixture_dir/ffmpeg-ok" "$fixture_dir/ffmpeg-fail"
