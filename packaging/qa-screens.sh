#!/bin/bash
# E4 · QML regression harness, dev-box edition.
#   1. qmllint over every QML file (the gate — nonzero exit on errors).
#   2. If the app is running under Hyprland: resize its window through
#      three widths (narrow / mid / wide) and screenshot each with
#      grim into qa-screens/<timestamp>/ for eyeball or image-diff.
# CI later swaps step 2 for an offscreen runner; the lint gate is
# already CI-shaped.
set -uo pipefail
cd "$(dirname "$0")/.."

echo "══ qmllint gate"
FAIL=0
for f in qml/*.qml; do
    OUT=$(qmllint -I build "$f" 2>&1)
    # Errors only — warnings are logged but don't gate.
    if echo "$OUT" | grep -q "^Error\|syntax error"; then
        echo "LINT FAIL: $f"
        echo "$OUT" | head -5
        FAIL=1
    fi
done
[ $FAIL -eq 0 ] && echo "qmllint: clean ($(ls qml/*.qml | wc -l) files)"

if ! command -v hyprctl >/dev/null || ! command -v grim >/dev/null; then
    echo "── no hyprctl/grim — skipping screenshot pass"
    exit $FAIL
fi
WIN=$(hyprctl clients -j | python3 -c "
import json,sys
for c in json.load(sys.stdin):
    if c['class'].lower() == 'zuuned':
        print(c['address']); break" 2>/dev/null)
if [ -z "${WIN:-}" ]; then
    echo "── zuuned not running — skipping screenshot pass"
    exit $FAIL
fi

OUT="qa-screens/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
for W in 640 1100 1900; do
    hyprctl dispatch resizewindowpixel "exact $W 900,address:$WIN" >/dev/null
    sleep 1.2
    GEO=$(hyprctl clients -j | python3 -c "
import json,sys
for c in json.load(sys.stdin):
    if c['address'] == '$WIN':
        print('%d,%d %dx%d' % (*c['at'], *c['size'])); break")
    grim -g "$GEO" "$OUT/w$W.png"
    echo "captured $OUT/w$W.png"
done
echo "── review: $OUT (diff against a blessed set when one exists)"
exit $FAIL
