#!/usr/bin/env bash
# Software-only photo stage probes, with optional preserved runtime captures.
set -euo pipefail
cd "$(dirname "$0")/../.."
source packaging/containers/smoke-runtime.sh
photo_fixture=$(mktemp -d /tmp/zuuned-photo-readiness.XXXXXX)
trap 'rm -rf -- "$photo_fixture"' EXIT
photo_checks=0
check_probe() {
    local stage=$1 expected=$2 file=$3 actual=0
    if photo_stage_is_painted "$stage" "$file"; then actual=1; fi
    [[ $actual == "$expected" ]] || { printf 'FAIL %s: expected %s got %s\n' "$stage" "$expected" "$actual" >&2; cat "$file"; exit 1; }
    printf 'PASS %s expected=%s\n' "$stage" "$expected"
    ((photo_checks+=1))
}
for stage in albums draft saved-parent parent-open; do
    ffmpeg -nostdin -y -v error -f lavfi -i color=black:s=1400x920 \
        -vf "$(photo_stage_probe_filter "$stage" "$photo_fixture/$stage.txt")" \
        -frames:v 1 -threads 1 "$photo_fixture/$stage.png"
    check_probe "$stage" 0 "$photo_fixture/$stage.txt"
    printf 'no stats\n' > "$photo_fixture/missing.txt"
    check_probe "$stage" 0 "$photo_fixture/missing.txt"
done
# Optional triples: stage, expected 0/1, preserved full-desktop screenshot.
while (($#)); do
    [[ $# -ge 3 && $2 =~ ^[01]$ ]] || exit 2
    stage=$1 expected=$2 screenshot=$3
    ffmpeg -nostdin -y -v error -i "$screenshot" \
        -vf "crop=1400:920:10:25,$(photo_stage_probe_filter "$stage" "$photo_fixture/evidence.txt")" \
        -frames:v 1 -threads 1 "$photo_fixture/evidence.png"
    check_probe "$stage" "$expected" "$photo_fixture/evidence.txt"
    shift 3
done
printf '%s photo readiness checks passed; no display or device\n' "$photo_checks"
