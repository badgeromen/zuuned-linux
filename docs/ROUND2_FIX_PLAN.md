# Round 2 Fix Plan — from the fresh-install rehearsal

34 findings from role-playing a stranger on a fully-stripped box (no
udev rule, no kernel module, no ~/.mtpz-data) against a real messy
library and both Zune families. Source: `docs/FRESH_RUN_LOG.md`.

**The headline first:** the core works. Both device families synced
real media end-to-end from a fresh AppImage on the stranger box (HD:
1.5 GB movie transcode→send→finalize; 120: tracks + album forge).
Every finding below is the gap between *works* and *delightful for
someone who isn't Orson* — not one is "the fundamentals are broken."

Already fixed live during the rehearsal: **#8** (house scrollbars),
**#9** (narrow-window now-playing teach), **#10** (kernel module
removed), **#33** (was a false alarm — HD profile → HD is correct).

---

## P0 — Data safety & correctness (before ANY tester syncs)

These can lose data or brick a device. Non-negotiable for Round 2.

**W1 · Folder lifecycle purge** — #17 — ✅ DONE 2026-09-02
LibraryDb::purgeFolder(path) cascade-deletes tracks/videos/photos
under a released folder via exact substr-prefix (NOT LIKE — proven to
spare siblings like music vs musicvideos vs music_extra), no
exclusion so re-adding rescans fresh. removeWatchFolder purges then
refreshes all models + rearms the watcher. Prefix logic unit-proven;
live click-test on the Settings remove button still pending.

**W2 · Sync progress = data safety** — #22 — ✅ DONE 2026-09-03
The per-item progress was already wired (video transcode reports frac
→ onTranscodeProgress → the sync bar). The missing piece was a PHASE
label: added SyncEngine.syncPhase (transcoding|sending|organizing|
finalizing), set at every transition. Panel now shows "transcoding
1/3 · A Goofy Movie" in orange with a "converting for your zune —
large videos take a few minutes" sub-line, so a long transcode reads
as work, not a hang. Kills the freeze→force-quit→bricked-object
chain. Live gate: watch a real movie transcode show the orange phase.

**W3 · Free space after delete** — #21 — ✅ DONE 2026-09-03
Root cause confirmed: the device won't report freed space until its
CleanDataStore re-index (at eject), so the pre-existing
workerRefreshStorage in onPurgeDone couldn't help mid-session. Added
noteBytesFreed (inverse of noteBytesWritten) + TrackModel size-sum;
onPurgeDone now credits the purged items' bytes (track filesize +
video/photo sizeMB) back to free space instantly, before dropping the
rows — the capacity gate stops refusing resyncs the moment a delete
lands. Authoritative refresh still fires for the eventual real number.

## P1 — First impression (the fresh-install path every tester walks)

**W4 · Scan visibility + refresh** — #15, #18, #19 — ✅ DONE 2026-09-03
Diagnosis corrected: the model refresh WAS wired (chunkCommitted +
finished both call reloadFromDb/reloadVideos). The real #15 bug was
the ONBOARDING summary — "0 movies found" and "0 tv episodes found"
were HARDCODED literals bound to nothing; now bound to new
LibraryService.movieCount / tvEpisodeCount. The visibility half (#19):
a pulsing-dot "scanning · stage · N/M" indicator under the page title,
app-wide, tied to LibraryService.scanning. The silent-success half
(#18/#19): a scanFoundNew signal → "added N tracks and M photos to
your library" toast on scan finish. Silent scans now announce
themselves.

**W5 · udev / install UX** — #29, #28, #4 — ✅ DONE 2026-09-03
- #29: renamed 45-zune.rules → 99-zune.rules (runs AFTER libmtp's
  69-, so our MTP_NO_PROBE / ID_MTP_DEVICE="" finally stick). Every
  ref updated (CMake install, README, build script, udevRuleOk).
- #28: a "Fix it for me" button on the C3 card → DeviceService.
  installUdevRule() runs pkexec (native password dialog, no terminal),
  writes the rule via inline printf (self-contained — verified
  byte-identical to the packaged file), reloads + triggers so no
  replug needed. Result toasts; udevRuleOk is now reactive so the
  card auto-hides on success. Manual self-contained tee command is the
  behind-a-link fallback for no-pkexec boxes.

**W6 · Folder picker theme** — #4, #5 — ✅ DONE 2026-09-03 (descoped)
Reframed: we already use Qt's native FolderDialog — nothing to
rebuild. The blinding white was main.cpp forcing
QT_QPA_PLATFORMTHEME=xdgdesktopportal (GTK portal follows the SYSTEM
theme → light Adwaita on a bare profile). Fix was two lines, not a
new component: (1) stop forcing the portal so FolderDialog uses Qt's
own dialog; (2) set a dark QGuiApplication palette (mirrors Theme
tokens) so that dialog — and any palette-driven control — comes up
dark on EVERY box, not just themed ones. Isolated test confirmed the
dark render; manual-path entry (#5) already present in both onboarding
and Settings. A user can still opt back into the portal via the env
var. NOTE: the dark app palette also touches all Basic controls —
in-app click-through is the final visual gate.

**W7 · Onboarding polish** — #1, #2, #3, #6, #7 — ✅ DONE 2026-09-03
Brand the welcome (wordmark + L15 mark) (#1); acknowledge a plugged-in
Zune "Zune HD detected ✓" (#2); mention mixtapes in the copy (#3); add
the ANIME folder type onboarding omits (#6); one-line the device
"sync theater" so nobody thinks it froze (#7).

## P2 — Core UX gaps (correctness + parity people hit early)

**W8 · Photo fixes** — #26, #30, #31
- #26: the ＋ opens fullscreen — the tile's fullscreen MouseArea eats
  the tap. Raise ＋/⌕ above it in hit order.
- #30: queue "count" is GROUP count; photos collapse to one album
  group so it never moves. Show item count.
- #31: solo photo sync may queue the wrong image — `parent.modelData`
  reach in the ＋ handler is the suspect; bind the cell's own
  modelData explicitly. Needs a live click-trace.

**W9 · Drag-to-queue parity** — #24, #25
UX-2 wired drag across all of music; video detail (episodes/seasons/
movies) and photos never got it. Port the album-page drag pattern to
episode rows, the season pill, the movie hero, and photo grids.

**W10 · Title display (wire-truth backed)** — #23, #11 — clean device-title
implementation and gates: [`VIDEO_TITLES.md`](VIDEO_TITLES.md).
Library shows the raw filename+extension. The real Zune displayed the
clean Name field ("Let You Down"), with series/season/episode as
dedicated fields (captured: WIRE_CAPTURE_FINDINGS.md). Show clean
titles; series as "S01E01 · <title>". Feed the query cleaner the junk
it still misses (#11: ENG-ITA pairs, RM4K, x.y channel layouts,
Surround/HDR/SDR, -Group@/(Group) tails).

**W11 · Manual fix-match mid-sweep** — #13
Manual match is locked out until the first matcher sweep finishes,
teaching users their input loses to the robot. Manual wins instantly,
mid-sweep included (user_edited stickiness already exists).

**W12 · Dual-format dedup** — #16
Real libraries ship FLAC+MP3 of the same album; both scan as rows and
would sync both to the Zune. Dedup by (artist/album/title/track) with
a format preference, or badge the format.

## P3 — Feature builds (designed/promised, not built)

**W13 · Manual editing everywhere** — #32
"Smart by default, manual override as backup" — only audio tags have
it today. Build the custom-artwork flow (file-pick → cache → sticky
override) that was called "THE HEART OF UX-3", designed on the
mockboard, and never built — for album/artist covers AND movie/series
posters. Add free-text video metadata editing. Surface track Edit
Info better; let edits reach device-side items.

**Library Customize completed 2026-09-07:** the approved Sleeve now includes
online album/artist matching, free Fanart artist portraits, persistent identity
choices, and genre/mixtape custom artwork. See [`SLEEVE_CUSTOMIZE.md`](SLEEVE_CUSTOMIZE.md).
Expanded on-device metadata/art editing remains separate from this local-library
completion. Virtual custom photo albums/nested subalbums follow this priority.

**W14 · Music Videos + Other, end to end** — #14
The Zune's own Videos menu has both sections; our pipeline half-knows
them (metagenres 0x23/0x21) but nothing user-facing exists. Folder
types in onboarding+Settings, library visibility, queue-to-device
under the right metagenre, and grab-back from those device sections.

**W15 · Settings redesign** — #20
"Truly suck", duplicated. Design-first: mock the new IA on the board
before rebuilding (house rule).

## P4 — Performance & reach

**W16 · Transcode acceleration** — #34
Linux video ENCODE is software x264 for everyone (the slow half — the
675s movie transcode lived here). `hwDecode` is NVDEC/CUDA — NVIDIA-
only, decode-only. AMD+Intel get ZERO acceleration. Add VAAPI decode
(AMD/Intel) and hardware ENCODE (VAAPI/NVENC) — the biggest
transcode-speed lever, and what makes big syncs bearable off NVIDIA.

**W17 · Compat AppImage** — (distro portability)
The AppImage is built on Arch (current glibc + Qt 6.7); older Ubuntu/
Mint LTS may refuse it. Build a compat AppImage in an older container
(newer Qt layered on an old-glibc base) targeting the oldest LTS worth
supporting — gate the target on a Discord "paste `ldd --version`".

## P5 — Polish

**W18 · Log-noise cleanup** — cosmetic
`[libusb] device not found (pid=0x0710)` on every HD connect (probes
classic PID first — reads like an error) and the repeated HD
`get_object_info 0x2002` (harmless, burns time). Quiet both.

---

## Suggested order

1. **W1, W2, W3** — data-safety trio. Ship blockers.
2. **W4, W5, W6, W7** — the fresh-install path. What makes Round 2
   testers succeed instead of bounce.
3. **W8, W10, W11** — early-hit correctness (photos, titles, fix-match).
4. **W9, W12, W14** — parity + real-library handling.
5. **W13, W15** — the big design-first builds (artwork, Settings).
6. **W16, W17, W18** — perf, reach, polish.

Status: drafted 2026-09-02 from the fresh-run rehearsal.

---

## Round 2.5 — transfer + live feedback (2026-09-03 session)

Emerged from the P0/P1 click-through and follow-on requests. **All of the
DONE items below are in the working tree and running in the test build
but NOT yet committed** (21 modified files) — commit is the next step.

**W19 · Launch fix (Qt 6.11)** — ✅ DONE — `DevicePanel.qml` missing
`QtQuick.Controls.Basic` import broke the whole UI silently after a
system Qt bump; added a QML-warnings printer in `main.cpp` so a bad
import can never hide again.

**W20 · Unified scan bar** — ✅ DONE — one continuous progress sweep
across music→videos→photos (grand total up front), human stage labels +
streaming filename. Was: per-phase bars that hit 100% and "froze".

**W21 · Device delete UX** — ✅ DONE — "deleting N of M" indicator +
freed-space now sticks (removed the stale device re-read that clobbered
the optimistic credit). Artist-level delete added (was missing).

**W22 · Drag transfer, both directions** — ✅ DONE (except playlists +
season level) — device→library and library→device at every card level
(song/album/artist, movie/series, photo/album, episode). Whole-island
drop zones that route by item type; matching entry lights up ("+ add to
library" / "＋ zune"); ghosts show real artwork. Pull-progress indicator
+ result/skip toasts. Largely subsumes **W9**.

**W23 · Dark folder picker** — ✅ DONE — `GTK_THEME=Adwaita:dark` pinned
for our process so the native GTK portal picker is dark on any profile.

### Still OPEN (tracked)

- **W24 · Playlists transfer + save-to-library** — ✅ DONE 2026-09-03.
  Library→device already worked (drag a playlist to the panel → sync).
  Device→library: drag a device cassette onto the library sidebar →
  recreate the playlist; owned tracks join now, misses are PULLED and
  auto-join once scanned. **Order fixed 2026-09-07:** schema v4 reserves durable
  ordered slots before downloads, then fills them in place. Repeated occurrences
  and original order survive restarts and out-of-order arrivals. Editor saves
  preserve pending/newly resolved entries that were not in the editor snapshot.
  Legacy QSettings notes migrate once, but already-scrambled playlists require
  reimport: those versions never stored their original positions. See
  [`../tests/playlist-order/README.md`](../tests/playlist-order/README.md).
- **W25 · Device TV-series drill-down redesign** — ✅ DONE 2026-09-03.
  The device drill-down now REUSES the approved `VideoSeriesDetail` in a
  new `deviceMode` (single source of truth, no drift): a QtObject adapter
  in DeviceVideosPage feeds it the Zune's series/episodes via
  seriesDetail()/episodesForSeason(). Library verbs (play, sync, mark-
  watched, fix-match) hide; rule-of-3 becomes save-to-library (＋→⤓,
  right-click Save/Delete, drag→library). Episode cards get the unified
  edge-lit hover. Also: episode cards gained the ＋ button + right-click
  in BOTH modes (rule of 3). v1 caveats: episode stills use device
  representative-sample art (blank until it loads / if none); no TMDB
  stills/ratings on device (device has none). The old inline seriesDetail
  Component in DeviceVideosPage is now DEAD CODE — remove in cleanup.
- **W26 · Season-level drag** (both directions) — ✅ DONE 2026-09-04.
  The "sync season" / "⤓ save season" hero verb is now dual-purpose:
  click acts, press-drag grabs the whole selected season (library→device
  or device→library by mode). Season selector is a ComboBox, so the verb
  is the grab handle.
- **W27 · Music delete from drill views** — ✅ DONE 2026-09-04. NOT a
  device-side bug: delete worked in the Songs tab (device accepts it),
  but the album-drill (AlbumDetail) and artist-drill (ArtistDetail) track
  rows never set `deletable`/`saveable` or the delete/save handlers — so
  right-click delete was only wired in the flat Songs list. Added the
  device rule-of-3 (delete + save) to both drill views' TrackListRows.
- **W28 · Rebuild AppImage against Qt 6.11** — the known-good AppImage
  bundles pre-6.11 Qt + pre-session code. (Overlaps W17.)
- **Commit** the 21 modified files (no AI attribution — house rule).
- **MusicPage.qml:362/605** recursive-rearrange layout warning — cosmetic
  (folds into W18).
