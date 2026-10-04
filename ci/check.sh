#!/usr/bin/env bash
# Reproducible software gates; fixtures own their disposable profiles/media.
set -euo pipefail
cd "$(dirname "$0")/.."
zuuned_results=$(mktemp -d "${TMPDIR:-/tmp}/zuuned-checks.XXXXXXXX")
printf 'Results: %s\n' "$zuuned_results"
git rev-parse HEAD > "$zuuned_results/source.txt"
git -C libzune rev-parse HEAD > "$zuuned_results/libzune.txt"
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
run public-source node tests/public-source/run.mjs
run build cmake --build build --parallel "${ZUUNED_BUILD_JOBS:-4}"
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
