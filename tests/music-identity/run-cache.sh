#!/usr/bin/env bash
set -euo pipefail
cache_test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cache_repo_dir=$(cd -- "$cache_test_dir/../.." && pwd)
cache_test_root=$(mktemp -d /tmp/zuuned-music-cache-build.XXXXXX)
trap 'rm -rf -- "$cache_test_root"' EXIT
read -r -a cache_qt_flags <<< "$(pkg-config --cflags --libs Qt6Core Qt6Network)"
c++ -std=c++20 -O1 -Wall -Wextra -fPIC -pthread -I"$cache_repo_dir/src" \
    "$cache_test_dir/cache.cpp" "$cache_repo_dir/src/library/MusicIdentityClient.cpp" \
    "${cache_qt_flags[@]}" -o "$cache_test_root/cache"
XDG_CACHE_HOME="$cache_test_root/cache-data" XDG_CONFIG_HOME="$cache_test_root/config" \
    "$cache_test_root/cache"
