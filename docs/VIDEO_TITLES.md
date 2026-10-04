# Clean video titles and quiet background scans

The device's `Name` is display text. `ObjectFileName` is the transfer filename.
Movies use their library title; episodes use their episode title (or `Episode N`
when unknown). Series, season and episode remain independent MTP properties.
Filename sanitization and the playable `.wmv`/`.mp4` suffix never alter `Name`.
Music already sends track titles separately from album metadata.

The app uses libzune's new `zune_smuggle_video_named` API. Legacy entry points
remain available for the Mac app. Readback has explicit title, filename and
format fields; a missing ZMDB filename stays unknown. Downloads reconstruct a
safe filename from metadata and format when needed. Episode duplicate detection
uses show/season/episode, never the globally ambiguous episode title.

An uploaded video whose metadata or readback failed is retained as `sent` with
a visible warning. It is persisted but never automatically re-uploaded. Old
warning rows do not count as new uploads in a later sync summary.

Interrupted video recovery stores a typed marker with both filename and title.
The recovery action resolves the actual filename through the serialized worker,
deletes only one exact matching video object, and keeps the marker on failure
or ambiguity. Previous plain-text video markers remain supported.

In the device video browser, **titles… → use library titles** previews existing
device title changes. Only unambiguous library identities are offered; every
selected row is checked again before its serialized Name-only rename. Filenames,
media bytes and series metadata remain intact. The app reports actual rename
completion; a disconnect or newly active device operation stops the remaining
work. No automatic mass rename occurs at connect.

The redundant scan strip above the video gallery was removed. Background
watcher/periodic scans continue, detailed progress remains in Settings, and
scan completion notifications still announce newly added content.

## Verification

- `bash tests/actions/video-run.sh`: production gallery stays in place while
  scanning starts, progresses and finishes; existing video actions remain wired.
- `node tests/video-titles/run.mjs`: native title/filename/dedup contracts,
  interrupted marker compatibility, and warning queue persistence.
- `node tests/video-titles/run-repair.mjs`: actual repair controller, isolated
  library and recording device, including ambiguity, stale state, failed renames,
  connection/operation guards and production sheet rendering.
- `bash libzune/tests/run-video-named.sh`: real MTP encoding/decoding against a
  recording transport with ASan, UBSan and LeakSanitizer.
- `libzune/zunetool video-title-test <short-wmv>`: hardware gate creates two tiny
  fixtures, reads back title/filename/episode properties, verifies a Name-only
  rename, deletes its own returned object IDs and finalizes.

Hardware gate **passed 2026-09-07 on the connected Zune 30 (Keel)**. A 13,699-byte
WMV was uploaded as movie object 33555819 and TV object 33555820. Name,
ObjectFileName, category, byte size/format, and TV series/season/episode read back
correctly. A Name-only rename preserved the filename, metadata and bytes. Both
fixture objects were deleted successfully; CleanDataStore finalized successfully.
Existing library objects were not renamed or deleted by this gate. Physical menu
appearance still requires looking at the Zune; protocol readback does not observe
its screen. Local run log: `/tmp/zuuned-title-hardware.log`.
