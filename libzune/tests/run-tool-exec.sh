#!/usr/bin/env bash
# External tools: no shell, exact argv, lookup through ZUNE_<TOOL>/PATH and
# private temporary outputs that never outlive a failure.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-tool-exec.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
mkdir "$fixture_dir/bin" "$fixture_dir/alt" "$fixture_dir/empty" "$fixture_dir/tmp"
# Recording fake: argv to $ZUNE_TOOL_LOG (NUL-separated), its own path to
# $ZUNE_TOOL_LOG.who, and its last argument as output. FAKE_TOOL_FAIL exits 3;
# FAKE_TOOL_EMPTY exits 0 without writing anything.
fake='#!/bin/sh
: > "$ZUNE_TOOL_LOG"
for arg in "$@"; do printf "%s\0" "$arg" >> "$ZUNE_TOOL_LOG"; done
printf "%s" "$0" > "$ZUNE_TOOL_LOG.who"
[ -n "${FAKE_TOOL_FAIL:-}" ] && exit 3
[ -n "${FAKE_TOOL_EMPTY:-}" ] && exit 0
'
create_output='for last in "$@"; do :; done
printf fake > "$last"
'
# ffprobe answers in ffprobe's flat format on stdout instead.
probe_output='printf "%s\n" "streams.stream.0.codec_name=\"mp3\"" \
  "streams.stream.0.codec_type=\"audio\"" "streams.stream.0.sample_rate=\"44100\"" \
  "streams.stream.0.channels=2" "streams.stream.0.disposition.default=1" \
  "format.duration=\"1.500000\"" "format.tags.title=\"Fake Probe Title\""
'
for tool in ffmpeg sips; do printf '%s%s' "$fake" "$create_output" > "$fixture_dir/bin/$tool"; done
printf '%s%s' "$fake" "$probe_output" > "$fixture_dir/bin/ffprobe"
printf '%s%s' "$fake" "$create_output" > "$fixture_dir/alt/ffmpeg-custom"
printf '%s%s' "$fake" "$probe_output" > "$fixture_dir/alt/ffprobe-custom"
printf '%s%s' "$fake" "$create_output" > "$fixture_dir/alt/not-executable"
chmod 755 "$fixture_dir"/bin/* "$fixture_dir/alt/ffmpeg-custom" "$fixture_dir/alt/ffprobe-custom"
chmod 644 "$fixture_dir/alt/not-executable"
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Isrc tests/tool_exec.c src/wiretap.c src/util.c src/photo.c src/upload.c src/thumbnail.c src/transcode.c \
    -Wl,--gc-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$fixture_dir/tool-exec"
env -u ZUNE_FFMPEG -u ZUNE_FFPROBE -u ZUNE_SIPS \
    PATH="$fixture_dir/bin:$PATH" ZUNE_TOOL_LOG="$fixture_dir/argv.log" TMPDIR="$fixture_dir/tmp" \
    ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/tool-exec" "$fixture_dir"
