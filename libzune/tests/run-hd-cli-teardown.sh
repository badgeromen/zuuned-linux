#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-hd-cli-teardown.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude -Isrc tests/hd_cli_teardown.c -o "$fixture_dir/hd-cli-teardown"
ASAN_OPTIONS=detect_leaks=1 "$fixture_dir/hd-cli-teardown"
