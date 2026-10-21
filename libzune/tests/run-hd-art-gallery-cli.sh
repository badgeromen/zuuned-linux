#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Every case is invalid locally and must exit before connect_or_die. This script
# never supplies a complete valid invocation and never accesses a device.
fixture_dir=$(mktemp -d /tmp/libzune-hd-art-gallery-cli.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
printf '\377\330\377\331' > "$fixture_dir/not-decoded.jpg"
guid=7c516f70-fb43-4d54-9cd3-5a4f6cf07890
image_id=11111111-2222-3333-4444-555555555551
checks=0
refuse() {
  set +e
  ./zunetool hd-art-serve "$guid" "$fixture_dir/not-decoded.jpg" "$@" > "$fixture_dir/out" 2>&1
  result=$?
  set -e
  if [ "$result" -ne 2 ] || grep -q -E 'Connecting|Connected:' "$fixture_dir/out"; then
    cat "$fixture_dir/out"
    echo 'FAIL: invalid gallery invocation reached connection or wrong exit' >&2
    exit 1
  fi
  checks=$((checks+1))
}
refuse --primary "$image_id"
refuse --gallery "$image_id"
refuse --gallery "$image_id" "$fixture_dir/not-decoded.jpg" --seconds 4
refuse --gallery "$image_id" "$fixture_dir/not-decoded.jpg"
refuse --gallery invalid "$fixture_dir/not-decoded.jpg"
refuse --gallery "$image_id" "$fixture_dir/not-decoded.jpg" --primary unknown
refuse --gallery "$image_id" "$fixture_dir/not-decoded.jpg" --primary "$image_id" --primary "$image_id"
refuse --gallery "$image_id" a --gallery "$image_id" b --gallery "$image_id" c --gallery "$image_id" d --gallery "$image_id" e --gallery "$image_id" f
refuse --album-sync
refuse --album-sync /no-such-zuuned-album-plan "$fixture_dir/manifest"
refuse --album-sync /no-such-zuuned-album-plan "$fixture_dir/manifest" --retain
refuse --album-sync /no-such-zuuned-album-plan "$fixture_dir/manifest" --artist-name Billie
refuse --album-sync /no-such-zuuned-album-plan "$fixture_dir/manifest" --first-sync x y
refuse --album-sync /no-such-zuuned-album-plan "$fixture_dir/manifest" --bind-on-ready 1 z
refuse --album-preflight
refuse --album-sync /no-such-zuuned-album-plan "$fixture_dir/manifest" --album-preflight --album-preflight
refuse --album-sync /no-such-zuuned-album-plan "$fixture_dir/manifest" --album-preflight --first-sync-preflight
refuse --album-resume
refuse --album-resume /no-such-zuuned-album-plan old
refuse --album-resume /no-such-zuuned-album-plan old old
refuse --album-resume /no-such-zuuned-album-plan old new --album-sync x y
refuse --album-resume /no-such-zuuned-album-plan old new --first-sync x y
refuse --album-resume /no-such-zuuned-album-plan old new --retain
refuse --thumbnail "$image_id" "$fixture_dir/not-decoded.jpg"
refuse --gallery "$image_id" a --thumbnail "$image_id"
refuse --gallery "$image_id" a --thumbnail "$image_id" b --primary "$image_id"
refuse --gallery "$image_id" a --thumbnail "$image_id" b --thumbnail "$image_id" c
refuse --gallery "$image_id" a --thumbnail invalid b
refuse --gallery "$image_id" "$fixture_dir/not-decoded.jpg" --thumbnail "$image_id" "$fixture_dir/not-decoded.jpg"
refuse --gallery "$image_id" a --gallery "$image_id" b --gallery "$image_id" c --gallery "$image_id" d --gallery "$image_id" e --gallery "$image_id" f --gallery "$image_id" g --thumbnail "$image_id" h
printf '\300\257' > "$fixture_dir/invalid-utf8.txt"
printf 'before\000after' > "$fixture_dir/nul.txt"
printf ' \r\n\t' > "$fixture_dir/whitespace.txt"
printf '' > "$fixture_dir/empty.txt"
head -c 4097 /dev/zero > "$fixture_dir/oversize.txt"
refuse --biography
refuse --biography /no-such-zuuned-biography
refuse --biography "$fixture_dir/empty.txt"
refuse --biography "$fixture_dir/whitespace.txt"
refuse --biography "$fixture_dir/oversize.txt"
refuse --biography "$fixture_dir/invalid-utf8.txt"
refuse --biography "$fixture_dir/nul.txt"
refuse --biography "$fixture_dir/empty.txt" --biography "$fixture_dir/empty.txt"
printf 'hd-art-gallery-cli: %s preconnection refusal checks passed\n' "$checks"
