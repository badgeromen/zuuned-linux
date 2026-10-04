# Queue controls and library location

User-approved UX changes, 2026-09-06, following the Sleeve action audit.

## Transfer queue

Individual entries and whole album/season groups have a permanent, subdued
remove control with a 32×32 hit target. Its slot is reserved, so hovering does
not move the title or make the action disappear. Right-click offers the same
removal action. Removal is guarded both in the view and at the panel handler
while sync is running; disabled controls absorb clicks instead of toggling
the group beneath them.

Series titles stay on the first line. Season/Specials leads the second line,
ahead of the episode count, so long titles cannot hide the season. Existing
queue contents, grouping keys, persistence, and sync behavior are preserved.

## Location marks

`OnDeviceBadge` now uses the user's ZUUNED app icon, with an “On this Zune”
tooltip. `qml/images/zuuned.png` is byte-identical to the packaged 64px icon;
keep the two aligned when updating the app branding. The Microsoft logo is
no longer referenced or bundled by the QML module.

Device song rows have a separate green monitor mark for “In your library.”
The shared row covers the songs view, album/artist/genre drills, and device
playlist members, including rows without artwork. The status column keeps
its width when membership changes.

Membership uses a strict, case-insensitive artist + album + title index,
accepting either the local track artist or album artist. Blank and unknown
metadata cannot produce a confident badge. A converted device MP3 can match
a local FLAC; this describes indexed library membership, not verified bytes
or currently mounted storage. Existing import deduplication rules remain
unchanged. A native hash index refreshes with the library, and lookups do no
disk/database I/O per row. A revision signal updates badges after scans,
imports, edits and deletions.

## Notifications

The old top-center warning capsule becomes a quiet bottom status strip,
aligned with the collection. Successful actions no longer show a warning
symbol. Long messages wrap within a bounded width; unusually long messages
can scroll. A new notice replaces the current one, avoiding a delayed backlog
from a large import. Hover/focus pauses dismissal, and action/dismiss controls
have 32px targets. Overlay placement keeps the strip readable around photos
and editor footers without covering the entire window with a hit area.

## Verification

- `node tests/actions/music/run.mjs` — shared row, app mark loading, and
  reactive device-row location status, alongside existing music actions.
- `PHOTO_TEST_INPUT=tests/actions/playlist bash tests/actions/photo-run.sh`
- `PHOTO_TEST_INPUT=tests/queue bash tests/actions/photo-run.sh`
- `bash tests/location/run-index.sh`
- `node tests/location/run-backend.mjs` — actual service and QML binding,
  metadata edit, generated WAV scan, and deletion in disposable storage.
- `bash tests/feedback/run.sh`

The final native build passed. Gates passed with 14 music UI checks, 8 playlist
UI checks, 10 queue UI checks, 9 notification UI checks, 13 identity-index cases,
and 8 real-service/QML-binding assertions. Qt UI totals include lifecycle hooks.
Queue and notification captures were visually reviewed at narrow and wide sizes.

These gates do not remove anything from the user's queue or send/delete any
media on a physical Zune. Native capture fixtures are used for visual review.
