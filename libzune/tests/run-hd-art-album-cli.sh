#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-hd-art-album-cli.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
printf 'ID3\000' > "$fixture_dir/not-a-transfer.mp3"
printf 'ZUUNED-HD-ALBUM\t1\nartist\tProbe\nalbum\tProbe Album\ntrack\t1\t1\t1000\tProbe\t%s/not-a-transfer.mp3\n' "$fixture_dir" > "$fixture_dir/plan.tsv"
printf 'invalid journal' > "$fixture_dir/journal"
chmod 600 "$fixture_dir/journal"
checks=0
refuse() {
  set +e
  ./zunetool hd-art-album-inspect "$@" > "$fixture_dir/out" 2>&1
  result=$?
  set -e
  if [ "$result" -ne 2 ] || grep -q -E 'Connecting|Connected:' "$fixture_dir/out"; then
    cat "$fixture_dir/out"
    echo 'FAIL: invalid album inspection reached connection or wrong exit' >&2
    exit 1
  fi
  checks=$((checks+1))
}
refuse
refuse "$fixture_dir/plan.tsv"
refuse /no-such-zuuned-album-plan "$fixture_dir/journal"
refuse "$fixture_dir/plan.tsv" "$fixture_dir/journal"
refuse "$fixture_dir/plan.tsv" "$fixture_dir/journal" --delete
printf 'hd-art-album-cli: %s preconnection inspection refusal checks passed\n' "$checks"
