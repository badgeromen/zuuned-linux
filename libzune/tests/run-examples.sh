#!/usr/bin/env bash
# Compile the real example entry points against ONLY the mock public API.
# No libzune archive, authentication input, USB backend or hardware is linked.
set -euo pipefail
cd "$(dirname "$0")/.."
example_fixture=$(mktemp -d /tmp/libzune-examples.XXXXXX)
trap 'rm -rf -- "$example_fixture"' EXIT
example_flags=(-std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -Iinclude)
for example in device_info spoken_inventory podcast_send audiobook_send; do
    "${CC:-cc}" "${example_flags[@]}" -Dmain="example_${example}" \
        -c "examples/$example.c" -o "$example_fixture/$example.o"
done
"${CC:-cc}" "${example_flags[@]}" tests/examples.c "$example_fixture"/*.o -o "$example_fixture/check"
ASAN_OPTIONS=detect_leaks=1 "$example_fixture/check" "$example_fixture"
