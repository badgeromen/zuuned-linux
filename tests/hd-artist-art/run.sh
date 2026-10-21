#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
c++ -std=c++20 -Wall -Wextra -Werror -fPIC -I"$repo/src" \
  "$repo/tests/hd-artist-art/store.cpp" "$repo/src/artwork/HdArtistArtStore.cpp" \
  $(pkg-config --cflags --libs Qt6Core Qt6Gui) -o "$test_dir/store"
"$test_dir/store"
