#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
public_source=${1:?Provide the reviewed public source directory}
public_only=${2:-}
[[ -z "$public_only" || "$public_only" == --public-only ]] || { echo 'Unknown authentication gate option.'; exit 2; }
[[ ! -e "$public_source/libzune/src/mtpz_keys.h" ]] || { echo 'Public header exclusion failed.'; exit 1; }
auth_test_root=$(mktemp -d /tmp/zuuned-public-auth-test.XXXXXX)
trap 'rm -rf -- "$auth_test_root"' EXIT
cc -D_GNU_SOURCE -std=c99 -ffunction-sections -fdata-sections -I "$public_source/libzune/src" tests/public-source/auth.c "$public_source/libzune/src/mtpz.c" -Wl,--gc-sections -Wl,--wrap=fopen -lgcrypt -o "$auth_test_root/public"
"$auth_test_root/public"
if [[ "$public_only" == --public-only ]]; then
    echo 'NOT APPLICABLE private embedded-auth comparison in public checkout.'
    exit 0
fi
cc -DEXPECT_EMBEDDED -D_GNU_SOURCE -std=c99 -ffunction-sections -fdata-sections -I libzune/src tests/public-source/auth.c libzune/src/mtpz.c -Wl,--gc-sections -Wl,--wrap=fopen -lgcrypt -o "$auth_test_root/private"
"$auth_test_root/private"
