#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-hd-tunnel-probe.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Isrc tests/hd_tunnel_probe.c \
    -o "$fixture_dir/hd-tunnel-probe"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/hd-tunnel-probe"
