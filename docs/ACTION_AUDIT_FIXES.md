# Media actions after the Sleeve audit

The user approved the audit fixes on 2026-09-06 after checkpoint `e43a4c5`
was committed and pushed to `ux/6-customize-design`.

## Connection contract

Adding entries to the Zune transfer queue requires a connected Zune. Media
dragging requires one too, with an approved September 27 exception: local photo
drags into the photo-album builder work offline. Those drags expose a separate
local target key, and expose the device-photo queue key only while connected.
Device drops and SyncEngine still recheck connection. An open music playlist
builder does not enable disconnected music dragging. Playback queues,
metadata/artwork editing, and playlist creation,
membership and order changes remain available without a device.

Views disable unavailable transfer actions and recheck at invocation, drop,
and picker completion. SyncEngine also rejects disconnected additions before
changing its queue, including playlist entries. Previously saved transfer
queues survive disconnects.

## Correctness

- Albums carry both album artist and album title through grouping, navigation,
  menus, pickers, playback, transfer, and device deletion. Compilations use
  album artist; same-title albums with different owners remain separate.
  Device snapshots lack album artist and fall back to track artist, so device
  compilation tracks can appear in separate artist groups.
- Songs play from the actual displayed, alphabet-filtered list.
- Starting at a later song explicitly starts its native playlist entry,
  avoiding the initial-load mismatch that played entry zero while displaying
  a different song. The initial load remains paused until mpv confirms the
  selected entry ID; a replaced queue's late completion cannot resume it.
- Video actions resolve current library data, so Apply's targeted repaint
  cannot leave queue/play/context-menu actions holding old metadata.
- Device episode controls never send device object IDs to local watched-state
  or metadata APIs. Their actions are save, rename, and delete as applicable.
- Edit Info returns separate library-save and media-tag results. Database
  failures keep the editable draft open and display an error. A row removed by
  a scan before the UI refreshes also fails the save without writing file tags.
- Confirmed device deletion results prune playlist/member browse snapshots;
  unsuccessful deletions leave their entries intact.
- Collapsed transfer-group artwork uses the group delegate's media type;
  it no longer references an ID scoped inside the separately loaded row.

## Interaction consistency

Existing album/artist Customize actions are reachable from their detail
artwork and nested album cards. Local movie, series, episode, and Needs Match
editing uses Sleeve's staged draft instead of the immediate legacy lookup.
Movie/episode/season/container artwork can be dragged while connected, and
episode controls are available in both wide and narrow layouts.

Photo thumbnails keep their quick action above the viewer click area. The
shared photo viewer and filmstrip use host-supplied local/device transfer
actions and payloads; device photos never pretend to have library IDs.
Device playlist member rows expose their existing save/delete/drag actions.
Playlist-builder metadata editing resolves the authoritative full track row.

Successful device music pulls now complete as one import batch. Zuuned adds
`Music/Zuuned Imports` to the music roots, scans it once, and immediately asks
the cached artist-image matcher for each distinct artist. It also fetches each
successful album's representative-sample JPEG from the Zune album object and
promotes it into the local album-art cache without replacing existing local or
custom artwork. The ZMDB artist record contains only an ID and name, so artist
portraits remain online/custom artwork with the flat fallback. Album, artist
and single-track saves share this path; failed extracts cannot create a watch
root or start an artwork request.

Songs offer Add to Now Playing and Play Next without a Zune. Play Next inserts
after the current song without resetting its file, pause state or position,
and mirrors that order into libmpv. Requested songs take priority over shuffle
on Next and natural track completion. It uses append plus
[`playlist-move`](https://mpv.io/manual/stable/#command-interface-playlist-move)
so it does not require the newer loadfile insertion flags.

Genre artwork editors, mixtape artwork overrides, photo editors and direct
playback from device files are separate features, not newly invented by this
action-parity pass. Offline playlist editing uses its ordinary controls.

## Verification

All fixtures use isolated data and recording boundaries; they do not send,
delete, rename, or synchronize anything on the user's Zune.

- `cmake --build build -j 4`
- `bash tests/customize/run.sh`
- `node tests/customize/run-backend.mjs`
- `node tests/customize/run-backend.mjs --player` — real libmpv, generated
  silent WAV files and null audio output; verifies the visible/native queue,
  preserved current playback, and actual next-song selection.
- `node tests/actions/music/run.mjs`
- `bash tests/actions/video-run.sh`
- `bash tests/actions/photo-run.sh`
- `PHOTO_TEST_INPUT=tests/actions/playlist bash tests/actions/photo-run.sh`
- `bash tests/device-music-import/run.sh`

Native QML results: 46 Sleeve/editor checks, 12 music checks, 16 video checks,
9 photo checks, and 8 playlist checks passed. Qt totals include lifecycle hooks.
The playlist checks click the actual edit/reorder/remove controls while offline.
All 27 real libmpv checks passed, including shuffle/manual Next/natural EOF,
preserved playback position and pause, rapid queue replacement, Previous during
loading, and recovery from missing or corrupt files. The final app build passed.

The backend gate covers successful library-only editing, a missing track,
blank input, a forced SQLite write failure, all four disconnected transfer
entry points, offline playlist creation, confirmed-deletion snapshots, and a
track removed from the database while its old UI snapshot remains available.
All 21 backend assertions passed.
