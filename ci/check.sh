#!/usr/bin/env bash
# Reproducible software gates; fixtures own their disposable profiles/media.
set -euo pipefail
cd "$(dirname "$0")/.."
zuuned_results=$(mktemp -d "${TMPDIR:-/tmp}/zuuned-checks.XXXXXXXX")
printf 'Results: %s\n' "$zuuned_results"
git rev-parse HEAD > "$zuuned_results/source.txt"
node ci/source-layout.mjs > "$zuuned_results/source-layout.json"
node -e 'const f=require("fs"); console.log(JSON.parse(f.readFileSync(process.argv[1])).revision)' "$zuuned_results/source-layout.json" > "$zuuned_results/libzune.txt"
zuuned_source_mode=$(node -e 'const f=require("fs"); console.log(JSON.parse(f.readFileSync(process.argv[1])).mode)' "$zuuned_results/source-layout.json")
git status --porcelain > "$zuuned_results/worktree.txt"
printf 'App: %s\nlibzune: %s\n' "$(cat "$zuuned_results/source.txt")" "$(cat "$zuuned_results/libzune.txt")"
if [[ -s $zuuned_results/worktree.txt ]]; then printf 'Working changes are present; this is a development verification.\n'; fi
run() {
    local name=$1
    shift
    printf 'RUN %s\n' "$name"
    if "$@" > "$zuuned_results/$name.log" 2>&1; then
        printf 'PASS %s\n' "$name" | tee -a "$zuuned_results/results.txt"
        if [[ ${ZUUNED_CI_LOGS:-0} == 1 ]]; then cat "$zuuned_results/$name.log"; fi
    else
        printf 'FAIL %s\n' "$name" | tee -a "$zuuned_results/results.txt"
        tail -n 80 "$zuuned_results/$name.log"
        exit 1
    fi
}
run configure cmake -B build -G Ninja
run source-layout-fixtures node tests/public-source/run-layout.mjs
if [[ $zuuned_source_mode == public-vendored ]]; then
    run public-source node tests/public-source/check-public.mjs
    run public-auth bash tests/public-source/run-auth.sh "$PWD" --public-only
    printf 'NOT APPLICABLE private exporter comparison (already-sanitized public checkout).\n' | tee -a "$zuuned_results/results.txt"
else
    run public-source node tests/public-source/run.mjs
fi
run build cmake --build build --parallel "${ZUUNED_BUILD_JOBS:-4}"
run audio-ranges bash tests/audio-ranges/run.sh
run spoken-media bash tests/spoken-media/run.sh
run spoken-native bash tests/spoken-media/native/run.sh
run spoken-organization bash tests/spoken-organization/run-qml.sh
run spoken-settings bash tests/spoken-organization/run-settings.sh
run queue-actions env PHOTO_TEST_INPUT=tests/queue bash tests/actions/photo-run.sh
run spoken-sync node tests/spoken-sync/run-backend.mjs
run device-spoken-qml bash tests/device-spoken/run-qml.sh
run device-spoken-backend node tests/device-spoken/run-backend.mjs
run spoken-inventory bash libzune/tests/run-spoken-inventory.sh
run podcast-protocol bash libzune/tests/run-podcasts.sh
run podcast-state bash libzune/tests/run-podcast-state.sh
run podcast-state-inspector node libzune/tests/podcast-state-inspector.mjs
run podcast-cleanup-worker node tests/podcast-cleanup-worker/run.mjs
run audiobook-protocol bash libzune/tests/run-audiobook-gate.sh
run media-capabilities bash libzune/tests/run-media-capabilities.sh
run video-protocol bash libzune/tests/run-video-named.sh
run identity bash tests/music-dedup/run-identity.sh
run migration bash tests/music-dedup/run-migration.sh
run sync node tests/music-sync/run-backend.mjs
run recovery node tests/recovery/run-backend.mjs
run audio node tests/audio-disc-tags/run.mjs
run audio-sanitized env SANITIZE=1 node tests/audio-disc-tags/run.mjs
run photo-db bash tests/photo-albums/run-db.sh
run photo-service node tests/photo-albums/run-backend.mjs
run photo-actions bash tests/actions/photo-run.sh
run photo-builder env PHOTO_TEST_INPUT=tests/photo-albums/tray bash tests/actions/photo-run.sh
run playlist-db bash tests/playlist-order/run-db.sh
run playlist-service node tests/playlist-order/run-backend.mjs
run playlist-actions env PHOTO_TEST_INPUT=tests/actions/playlist bash tests/actions/photo-run.sh
run music-actions node tests/actions/music/run.mjs
run music-import bash tests/music-import/run.sh
run local-music-copies bash tests/local-music-copies/run.sh
run location-index bash tests/location/run-index.sh
run location-service node tests/location/run-backend.mjs
run provider bash tests/music-identity/run.sh
run artwork-discovery bash tests/artwork-discovery/run.sh
run artwork-persistence bash tests/artwork-persistence/run.sh
run customize node tests/customize/run-backend.mjs
run customize-qml bash tests/customize/run.sh
run provider-cache bash tests/music-identity/run-cache.sh
run provider-shutdown node tests/provider-shutdown/run-backend.mjs
run diagnostics bash tests/diagnostics/run-core.sh
run diagnostics-report bash tests/diagnostics-report/run.sh
run usb-rules bash tests/udev-setup/run.sh
run device-import bash tests/device-music-import/run.sh
run video-titles node tests/video-titles/run.mjs
run video-repair node tests/video-titles/run-repair.mjs
run artwork-pipeline bash tests/artwork-pipeline/run.sh
printf 'All software gates passed. Evidence: %s\n' "$zuuned_results"
