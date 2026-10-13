# Spoken-media device queue gates

`node tests/spoken-sync/run-backend.mjs` links the current application's native
objects. Run `cmake --build build` first. It creates an isolated profile and never
constructs a USB service. The 56 checks cover queue identity, same-title episodes,
feed scoping, grouping, capacity, restart persistence, stale completion, successful
and duplicate completion, music isolation and conservative interruption recovery.
Confirmed podcast cleanup clears the durable marker while preserving the actual
queue error; a missing object ID or stale completion cannot waive recovery.
Audiobook checks additionally cover content identity, reviewed part ordering,
typed persistence and separate completion counts.
It is included in `bash ci/check.sh`.

`bash tests/spoken-media/native/run.sh` covers native RSS/download/storage/QML,
actual pointer gestures, playback, immediate quiet drafts and deferred download/preparation using a
loopback server and a disconnected-device stub. It does not use USB.

For an explicitly authorized hardware transfer, close the normal app and wait
for its process to exit. The hardware harness uses the actual app services and
worker, retains the selected episode on the device, finalizes, and keeps private
logs. It does not purge existing media. Select one real feed-scoped GUID:

```sh
SPOKEN_SYNC_FEED=https://rss.buzzsprout.com/2534968.rss \
SPOKEN_SYNC_EPISODE=guid:Buzzsprout-19376357 \
node tests/spoken-sync/run-backend.mjs --hardware
```

The harness first verifies immediate pending draft insertion. Starting sync
then triggers download/preparation and a typed device send, or successful reuse
of an existing episode. It checks completion counts, queue removal and session release.
Raw logs remain mode 0600 under the reported temporary directory and may contain
authentication data. Only explicitly tagged result lines are printed. Do not
publish those raw logs. See the sanitized Keel evidence in the libzune fixture
folder and `docs/SPOKEN_MEDIA_AND_DEVICE_ART.md` for precise hardware limits.

For the audiobook application gate, supply a directory with short `01.mp3`,
`02.mp3` and `cover.jpg` fixtures. It imports a two-part "ZUUNED GUI Book Test",
queues the whole book immediately, then prepares and sends through the actual
app services after starting sync. Repeat runs skip existing parts without
duplicating device objects. This writes and retains test media on the connected
Classic or HD, so run only for an authorized device test with Zuuned closed:

```sh
SPOKEN_SYNC_BOOK_DIR=/path/to/fixtures \
node tests/spoken-sync/run-backend.mjs --hardware-book
```

The isolated profile has no normal library state. No USB mode runs in CI.
Art USB acceptance is separate from visual presentation and playback/resume on
the device. For a chaptered whole-book transfer, also set `SPOKEN_SYNC_CHAPTER_FILE` to a
local unprotected M4B fixture. The harness imports its whole book and verifies
that missing chapter rows, rather than repeated whole-source payloads, complete.
The source directory still supplies `cover.jpg`. This hardware mode is manual.

Played-podcast cleanup orchestration also runs against the production SyncEngine
with an isolated no-watcher facade and disconnected worker signals. It covers
empty-queue opt-in, inspect/persist/purge ordering, stale callbacks, cancellation,
persistence refusal, confirmed-only partial results, current-device consumed
history and suppression of requeued played episodes. The separate
`tests/podcast-cleanup-worker/run.mjs` gate wraps device I/O for the actual worker.
Neither software gate accesses USB. Final GUI deletion on Classic/HD remains a
user acceptance gate.
