#!/usr/bin/env bash
# Software-only readiness policy; no application, display or device is opened.
set -euo pipefail
cd "$(dirname "$0")/../.."
source packaging/containers/smoke-runtime.sh
playlist_fixture=$(mktemp -d /tmp/zuuned-playlist-readiness.XXXXXX)
trap 'rm -rf -- "$playlist_fixture"' EXIT
playlist_checks=0
check_probe() {
    local name=$1 expected=$2 actual=0
    if playlist_import_is_painted "$playlist_fixture/$name.txt"; then actual=1; fi
    [[ $actual == "$expected" ]] || { printf 'FAIL %s: expected %s got %s\n' "$name" "$expected" "$actual" >&2; exit 1; }
    printf 'PASS %s\n' "$name"
    ((playlist_checks+=1))
}
synthetic() {
    local name=$1 expected=$2 source=$3 overlay=${4:-null}
    ffmpeg -nostdin -y -v error -f lavfi -i "$source" \
        -vf "$overlay,$(playlist_import_probe_filter "$playlist_fixture/$name.txt")" \
        -frames:v 1 -threads 1 "$playlist_fixture/$name.png"
    check_probe "$name" "$expected"
}
synthetic empty-page 0 'color=black:s=1400x920'
synthetic white-splash 0 'color=white:s=1400x920'
synthetic dim-background 0 'color=0x101010:s=1400x920' \
    'drawbox=x=480:y=815:w=60:h=12:color=0x555555:t=fill'
synthetic wrong-location 0 'color=0x101010:s=1400x920' \
    'drawbox=x=480:y=600:w=60:h=12:color=white:t=fill'
synthetic actual-label-location 1 'color=0x101010:s=1400x920' \
    'drawbox=x=480:y=815:w=60:h=12:color=white:t=fill'
printf 'no signal statistics\n' > "$playlist_fixture/missing.txt"
check_probe missing 0
# Optional pairs: expected readiness (0/1), preserved 1440x1000 screenshot.
while (($#)); do
    [[ $# -ge 2 && $1 =~ ^[01]$ ]] || { echo 'Expected pairs: 0|1 screenshot' >&2; exit 2; }
    expected=$1 screenshot=$2
    name="preserved-$playlist_checks"
    ffmpeg -nostdin -y -v error -i "$screenshot" \
        -vf "crop=1400:920:10:25,$(playlist_import_probe_filter "$playlist_fixture/$name.txt")" \
        -frames:v 1 -threads 1 "$playlist_fixture/$name.png"
    check_probe "$name" "$expected"
    shift 2
done
printf '%s playlist readiness checks passed; no display or device\n' "$playlist_checks"
