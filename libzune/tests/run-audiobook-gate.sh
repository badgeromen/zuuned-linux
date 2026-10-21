#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
make libzune.a >/dev/null
cc -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Wno-unused-function \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
  -Iinclude -Isrc tests/audiobook_gate.c libzune.a \
  -Wl,--wrap=zune_breach -Wl,--wrap=mtp_get_object_prop_value -Wl,--wrap=mtp_get_object_handles \
  $(pkg-config --libs libusb-1.0 libgcrypt libavformat libavutil) -o "$tmp/gate"
ASAN_OPTIONS=detect_leaks=1 "$tmp/gate"
