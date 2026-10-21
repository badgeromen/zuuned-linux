#!/usr/bin/env bash
# T1.7: fail each MTPZ handshake allocation in turn against a fake device; the
# handshake must fail cleanly (no crash, leak or later send). No USB.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-mtpz-nullcheck.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
# The ported hash and AES code read 32-bit words at odd offsets and shift into
# the sign bit (x86 and GCC handle both); that math is out of scope, so those
# three UBSan checks are off for this file. ASan, leaks and the rest stay on.
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize=alignment,shift,signed-integer-overflow \
    -fno-sanitize-recover=undefined -pthread -Iinclude -Isrc tests/mtpz-nullcheck.c src/wiretap.c \
    -fsanitize=address,undefined -fno-sanitize=alignment,shift,signed-integer-overflow -lgcrypt -o "$fixture_dir/mtpz-nullcheck"
# libgcrypt keeps its random pool from the test's key generation until exit.
printf 'leak:gcry_pk_genkey\n' > "$fixture_dir/lsan.supp"
ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=suppressions="$fixture_dir/lsan.supp":print_suppressions=0 \
    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    "$fixture_dir/mtpz-nullcheck" 2>"$fixture_dir/stderr" || { tail -30 "$fixture_dir/stderr"; exit 1; }
