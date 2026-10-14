#!/usr/bin/env bash
# Software-only client-frame fixtures. No app, display, USB or desktop mutation.
set -euo pipefail
cd "$(dirname "$0")/../.."
source packaging/containers/smoke-runtime.sh
readiness_fixture=$(mktemp -d /tmp/zuuned-runtime-readiness.XXXXXX)
trap 'rm -rf -- "$readiness_fixture"' EXIT
readiness_checks=0
probe() {
    local name=$1 expect=$2 source_filter=$3 extra_filter=${4:-null}
    ffmpeg -hide_banner -nostdin -y -v error -f lavfi -i "$source_filter" \
        -vf "$extra_filter,$(onboarding_probe_filter "$readiness_fixture/$name.txt")" \
        -frames:v 1 -threads 1 "$readiness_fixture/$name.png"
    local actual=0
    if onboarding_is_painted "$readiness_fixture/$name.txt"; then actual=1; fi
    [[ $actual == "$expect" ]] || { echo "FAIL $name: expected $expect got $actual" >&2; exit 1; }
    printf 'PASS %s\n' "$name"
    readiness_checks=$((readiness_checks+1))
}
probe unpainted-black 0 'color=black:s=1400x920'
probe unpainted-white 0 'color=white:s=1400x920'
probe painted-title-with-unpainted-controls 0 'color=0x0a0a0a:s=1400x920' \
    'drawbox=x=460:y=300:w=300:h=60:color=white:t=fill'
for dimensions in 1400x920 1280x800; do
    width=${dimensions%x*}; height=${dimensions#*x}
    probe "painted-controls-$dimensions" 1 "color=0x0a0a0a:s=$dimensions" \
        "drawbox=x=$((width/2+162)):y=$((height-57)):w=70:h=12:color=0x606060:t=fill"
done
printf 'not signalstats\n' > "$readiness_fixture/missing.txt"
if onboarding_is_painted "$readiness_fixture/missing.txt"; then
    echo 'FAIL missing signalstats accepted' >&2; exit 1
fi
printf 'PASS missing signalstats rejected\n'
readiness_checks=$((readiness_checks+1))
# Optional preserved failed-run full-screen captures. These known fixture
# windows have 1400x920 client pixels at root (10,25); the old probe saw only
# background. Exercise the same crop/filter against the actual painted client.
for screenshot in "$@"; do
    name="evidence-$readiness_checks"
    ffmpeg -hide_banner -nostdin -y -v error -i "$screenshot" \
        -vf "crop=1400:920:10:25,$(onboarding_probe_filter "$readiness_fixture/$name.txt")" \
        -frames:v 1 -threads 1 "$readiness_fixture/$name.png"
    onboarding_is_painted "$readiness_fixture/$name.txt" || { echo "FAIL preserved painted frame: $screenshot" >&2; exit 1; }
    printf 'PASS preserved painted client: %s\n' "$screenshot"
    readiness_checks=$((readiness_checks+1))
done
printf '%s runtime paint-readiness checks passed; no display or device\n' "$readiness_checks"
