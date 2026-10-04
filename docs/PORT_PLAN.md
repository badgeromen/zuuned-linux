# ZuunedLinux Port Plan

> Implementation history and remaining verification backlog. The dated phases
> below include original plans and later corrections; they are not a promise
> that every item ships in the current AppImage. For the current public layout
> and download/source distinction, see [the documentation guide](README.md).

The complete, phase-by-phase sequence for porting ZuunedMac to Linux.
Derived from a full analysis of the mac codebase (all services, views,
dialogs, and the transcode C layer) on 2026-08-27. Each phase has a
**hardware/verification gate** — it is not done until the gate passes,
per AGENTS.md ("hardware claims require a real device test").

**Display font replacement, 2026-10-01:** Specimen Alien replaces Misdemeanor
in app resources. Existing graffiti selections retain their role through a
legacy-setting alias; Permanent Marker remains the default. The generator,
coverage and preserved original Specimen study are in `tools/font-studio/`.
This font replacement was packaged in 0.1.0 and retained in 0.1.1. Later
source changes need their own package validation.

**Music matching follow-up, 2026-10-02:** artist portraits and album covers
share the phased plan in [MUSIC_MATCHING_PLAN.md](MUSIC_MATCHING_PLAN.md).
Status: native software candidate; user review and new packaging remain.
Preserve local/manual artwork and metadata; resolve aliases,
score edition evidence, apply confident general covers automatically and expose
lookup status. Uncertain identity still needs a manual match.

## Ground rules (apply to every phase)

**Local music copies, 2026-10-02:** Phase 3 follow-up groups verified audio
copies across folders without deleting source rows or changing saved playlist
references. Schema v9 and native gates are described in
[LOCAL_MUSIC_COPIES.md](LOCAL_MUSIC_COPIES.md). Packaging remains separate.

**Feature scope, 2026-09-27:** the active follow-on sequence is documented in
[`FEATURE_COMPLETION.md`](FEATURE_COMPLETION.md): custom/nested photo albums,
disc-aware device dedup, on-device metadata/artwork editing, hardware encoding.
Device-browser video playback is out of scope. Classic per-media storage byte
discovery is deferred for a separate protocol/reverse-engineering effort.

**Approved UX deviation, 2026-09-06:** the user selected the original Sleeve
customization design for the native app. Its layout, theme additions, shared
Apply/Cancel draft, artwork pinning, and validation are documented in
[`SLEEVE_CUSTOMIZE.md`](SLEEVE_CUSTOMIZE.md). This supersedes the older boxed
customization sheet's presentation; the device protocol and hardware gates
are unchanged.

**Approved queue/feedback changes, 2026-09-06:** larger queue removal targets,
the user's ZUUNED app mark for on-device status, device-song library membership
indicators, and bottom notifications supersede the old top-warning capsule.
See [`QUEUE_AND_FEEDBACK.md`](QUEUE_AND_FEEDBACK.md) for the behavior and gates.

**Clean video titles, 2026-09-07:** user-approved separation of device display
titles from transfer filenames, reviewable title repair, and quiet video-gallery
scans are specified in [`VIDEO_TITLES.md`](VIDEO_TITLES.md).

**Library Customize and playlist order, 2026-09-07:** Sleeve music identities,
free Fanart portraits, genre/mixtape artwork and ordered pending playlist imports
are implemented. Schemas v4/v5 preserve import occurrences and collection choices.
See [`SLEEVE_CUSTOMIZE.md`](SLEEVE_CUSTOMIZE.md) and the isolated
[`playlist gates`](../tests/playlist-order/README.md). Virtual custom photo albums
with nested subalbums were added September 27 alongside the existing Phase 8
folder browser. Local photo organization works without a Zune; transfers to the
device still require a connection.

**Artwork print Appearance integration, 2026-09-08:** the user approved shipping
Original / Clean ink / Halftone / Worn print choices with a live sample,
collapsed per-look fine-tuning and independent persistent preferences. Original
is the default. Accepted print defaults are Detail 100, Halftone strength 14 /
Dot size 17 / color, and Worn wear 14. Reset affects only the selected look.
The shared native display pipeline treats selected local artwork while keeping
source URLs, metadata and transfer bytes unchanged. Photos, wallpapers, app
marks and remote provider candidates retain original rendering. This expands
Phase 1's Appearance settings without changing any hardware gate. The software
gate passes: native build, 58 worker/cache checks, 12 OpenGL display passes,
settings persistence/interactions and 117 existing surface/action checks. The
production Appearance capture and app launch are free of QML/rendering warnings.
See [`ARTWORK_STYLE_STUDY.md`](ARTWORK_STYLE_STUDY.md).

- **Port, don't invent.** Every view/behavior traces to a named Swift
  source file. Deviations get a written reason.
- **Thread discipline.** All libzune calls on DeviceWorker (one thread,
  serialized). Transcode on its own QThreadPool — never behind USB ops.
  Each mpv player owns its own context and event thread. UI thread owns
  state; queued signals only.
- **Protocol changes belong in libzune**, hardware-tested via zunetool.
  Coordinate them with the standalone library repository, then update the
  application's included source. The private development checkout uses a
  submodule; this public snapshot vendors its files. Never patch protocol
  behavior into application-side workarounds.
- **Theme tokens only** in QML; token changes ported from
  `ZuneColors.swift`/`ZuneStyles.swift`/`ZuneFonts.swift`, never local.
- Every phase ends with updated evidence and a reviewed commit. Pushing
  requires the user's explicit approval under AGENTS.md.

## Already done (foundation)

| Area | State |
|---|---|
| Protocol stack (breach/MTPZ/ZMDB/send/purge over libusb) | hardware-proven |
| Wire fixes (ZLP, autopsy, triage, mid-transfer abort, OpenSession auto-reset) | on `fix/wire-protocol`, hardware-proven |
| App chrome (rotated gradient header, 3-column, sidebar, pivots) | ported |
| GrungeGlass + spray EnvironmentBackground | ported (wallpaper layer pending) |
| Device panel (tabs, browse menu, disconnected state) | ported; sync/queue wiring pending |
| Device music browser (pivots, drill-downs, search, sort) | ported |
| Album art from device (album-object routing, disk cache) | hardware-proven |
| Device videos/pictures/playlists pages (flat lists) | basic |
| Hotplug auto-connect, DeviceWorker serialization | done |

---

## Phase 1 — Shell & Feel completion

*Goal: the app FEELS like Zuuned before more features land.*

1. **SplashView port** (`SplashView.swift`): Misdemeanor 96pt, three
   text layers — white base, gradient revealed by left-anchored mask
   (1.8s easeInOut), 30px blurred pink scan-line sweep; "for zune"
   subtitle at 1.2s; minimum 1.5s, 0.5s fade to main UI.
2. **DevicePresence bloom** (`DevicePresence.swift`): connect
   choreography — spray shift (0s) → warmth bloom (0.15s) → mood color
   fade (0.3s); reverse on disconnect. Mood color = random biased by
   dominant device genre (`ZuneColors.deviceMoodColor`, 75/25 rule).
   Wire into EnvironmentBackground + GrungeGlass `deviceColor`.
3. **Toast system** (`ContentView` toast overlay): top strip, warning
   icon + message + optional action, auto-animated, zIndex 200. Add a
   `ToastHost` + `AppShell` signal API — dialogs/sheets route the same
   way (a `Dialogs` Loader host replacing AppState's `.sheet` chain).
4. **Motion polish**: device panel slide (`.move(edge:.trailing)` +
   opacity, `ZuneMotion.slow`), animated page swaps, panel toggle icon.
5. **Settings shell** (`SettingsView.swift`): 4-tab PivotBar
   (Appearance / Media / Sync / About). Ship Appearance NOW: wallpaper
   picker (`backgroundImagePath` via QFileDialog), opacity 0.05–0.8,
   blur 0–50 → add the wallpaper layer to EnvironmentBackground;
   header-font radio (`headerFont`) → GradientText + splash.
6. **QSettings registry**: one C++ `Settings` QML singleton exposing
   every mac key with defaults: `onboardingComplete`, `headerFont`,
   `backgroundImagePath/Opacity/BlurAmount`, `videoProfile` (default 2),
   `preferredAudioLang` ("eng"), `posterSource` ("tmdb"),
   `tmdbApiKey`/`fanarttvApiKey`, `videoMinSizeMB`, `miniPlayerFrame`.
7. **Connection staging**: DeviceService `connectionStage/Progress`
   (mirrors `AppState.connectDevice`) feeding the panel's ConnBar;
   distinct `connectionError` property.
8. **Device picture thumbnails** (quick win): `doGrabPhotoThumb`
   worker slot (`zune_grab_photo_thumb`), thumbnail grid on the device
   pictures page; folder grouping by `parentId` + `zune_get_folders`
   names (start of the full `DevicePicturesView` port, finished Phase 8).

**Gate:** splash → main flows; connect bloom visible; wallpaper +
header font settings persist across restart; device photos show thumbs.

---

## Phase 2 — Transcode C layer (foundation for library, sync, video)

1. Compile `libav_transcode.c` + `id3_rewrite.c` into the Linux build
   against **system FFmpeg 9 + lame** (pkg-config; CMake target).
   Only ~60 lines are mac-specific: drop the VideoToolbox hw-device
   block + `get_vt_hw_format`; PROFILES 1–3 `h264_videotoolbox` →
   `libx264` (+`preset=fast`); verify profile 0 selects **wmav2**
   audio (not AAC) into ASF.
2. libzune Linux build: enable the in-process probe path (`USE_LIBAV`)
   or route `zune_probe` needs through the app-side probe functions —
   decide once, document in AGENTS.md.
3. **`transcodetool` CLI gate** (like zunetool): arm-audio any→MP3,
   video profiles 0–3, probe, extract-art, extract-frame — verifiable
   without the app. Play outputs in mpv; byte-check ID3v2.3 output.

**Gate:** FLAC→MP3 and MKV→profile-2 MP4 and profile-0 WMV transcode
on this machine; ID3 output opens in a tag editor as v2.3.

---

## Phase 3 — Local Library (the sidebar comes alive) + Start screen

*Mac sources: `LibraryService.swift` (4k lines), `ImportSheet`,
`OnboardingView`, art services.*

1. **LibraryDB (C++)**: raw sqlite3 (same C API as mac — schema ports
   verbatim), WAL, `QStandardPaths::AppDataLocation/library.db`.
   Tables: tracks/videos/photos/watch_folders/playlists/
   playlist_tracks/sync_rules/sync_state/excluded_files/artists.
   `user_edited` never-clobber + `excluded_files` semantics. (Skip the
   mac's one-time migrations — Linux starts clean.)
2. **Scanner**: 3-phase per type (serial prep w/ mtime fast-path →
   parallel probe on QThreadPool, chunks 64/32/128 → serial chunked
   transactional upsert) with streaming refresh signals. Metadata
   fallback: tags → `Folder/Artist/Album` → `Artist - Title` filename.
   No FS watching — manual scan, as on mac.
3. **Album grouping**: canonical key `COALESCE(NULLIF(albumartist,''),
   artist)`, NOCASE collation.
4. **AlbumArtService**: folder art (cover/folder/artwork/album/front)
   → any image → embedded via `zuuned_extract_art`; cache key
   **byte-identical MD5("artist\nalbum" lowercased)** — device art
   sync depends on it. QCache LRU (400 decoded, ≤400px).
5. **Local models → views**: C++ QAbstractListModels for local
   tracks/videos/photos; music browser re-targets local data (device
   browser keeps device data — shared components, as mac shares
   CollectionView/DeviceMusicView).
6. **Onboarding** (`OnboardingView.swift`): 3 steps — welcome; typed
   folder rows (music/movies/tv/photos) + poster-source picker; scan
   kickoff with live counts, "open zuuned (scan continues)". Shows
   whenever the library is empty. **This is the start-screen item.**
7. **Settings Media tab**: typed watch-folder management (add/remove/
   Treat As), `videoMinSizeMB`.
8. **ImportSheet + CopyFromDeviceSheet** progress UIs; device
   copy-to-library actions (`zune_extract_track/_video/_photo`) wired
   to the header actions + context menus.
9. **Metadata dialogs**: MetadataEditSheet, AlbumEditSheet,
   AlbumArtPickerSheet (MusicBrainz/CAA/Fanart backends),
   LookupSheet + AcoustID (QProcess `fpcalc`, optional, degrade
   gracefully).
10. **Enrichment consumers** (each independent, offline-tolerant):
    Deezer genre/year, artist images (MusicBrainz→Fanart→Deezer),
    music-art candidates. Respect rate limits (MusicBrainz 1.1s + UA).

**Gate:** onboarding on fresh start → point at real ~/Music → library
scans, streams into the UI, album art appears, edits persist, rescan
respects user_edited.

---

## Phase 4 — Music Player

*Mac sources: `PlayerService`, `MPVPlayer`, `MusicMiniPlayerView`,
`QueueRibbonBar`, `VinylRecordView`, `CurvedText`.*

1. **MpvController (C++)**: audio (`vo=null`) + video context
   variants; Linux options (`hwdec=auto-safe`, default ao); event
   thread with 0.1s/1s adaptive `mpv_wait_event`, **4 Hz time-pos
   throttle at the source**; command/property helpers; track-list JSON.
2. **PlayerService port**: queue mirrored into mpv playlist
   (`loadfile append`), EOF auto-advance with *sync-only* handler +
   `isJumping` guard (0.3s), shuffle (random non-current), repeat
   off/all/one, prev-restarts-if->3s, 5s position-save timer into
   LibraryDB.
3. **Vinyl assembly**: SpinningDiscView; VinylRecordView port —
   chapter-ring grooves (adaptive count ~45s/ring, clamp 5–20, ±1%
   seeded jitter), gradient progress trail + glowing seek dot,
   ring-tap chapter jump, cumulative-angle drag seek, accumulator
   rotation ticker (advances only when playing && visible), **15 Hz
   interpolated progress anchored on 4 Hz real updates**.
4. **QueueRibbonBar** (top overlay): played cards | 90px spinning disc
   with angular progress ring + seek dot | upcoming cards; equal fixed
   side widths; shuffle/repeat/mini-player/save-queue buttons; cards
   are static children so position ticks don't re-render art.
5. **Mini player**: frameless always-on-top transparent QML Window,
   `startSystemMove()` drag, 280px vinyl + CurvedText 80° arc title,
   hover UP-NEXT drawer (reserved space, 450ms retract), geometry in
   `miniPlayerFrame` (Wayland: size only + Hyprland float rule).
6. Play/queue actions wired from all music rows/detail buttons.

**Gate:** play a local album start→finish; queue ribbon parity;
mini-player drag + seek on Hyprland; positions resume after restart.

---

## Phase 5 — Sync Engine (the reason the app exists)

*Mac sources: `SyncQueueService`, `SyncManager`, transcode layer.*

1. **Queue core**: `SyncQueueEntry` + QueueModel (QAbstractListModel);
   mac dedup rules (libraryId | filepath | title+artist); add-to-queue
   context menus + addAlbum/addPhotoAlbum; DevicePanel queue tab rows
   (art thumb, per-item progress, status icons, remove) — the panel
   scaffolding already awaits exactly this.
2. **DeviceWorker send slots**: `doSendTrack` (smuggle_track_tagged +
   `zune_verify` warning-only), `doForgeAlbums`, `doSendPhoto`,
   `doSendVideo`, `doCreatePlaylist`, `doFinalize`; C progress
   callback → queued per-item signal; cross-thread `zune_abort`
   accessor (volatile flag — safe) wired to the cancel button
   (mid-transfer abort already in libzune: fixes mac Known Issue #2).
3. **`awaitingDisconnect` flow**: sync completion does NOT finalize.
   "disconnect your Zune to apply changes" → eject = forceStop →
   clearQueue → finalize (CleanDataStore re-index) → sever. **The flag
   must suppress hotplug auto-breach** or we reconnect into a
   re-indexing device.
4. **Milestone A — MP3-only sync** (no FFmpeg needed):
   `canonicalizeAlbumArtists` ported verbatim (album+disc-folder
   bucketing, mode/VA/shortest rules) → device dedup
   (`artist\talbum\ttitle` lowercased) → `zuuned_retag_mp3` with
   albumartist → send → **Phase-2 albums**: forge_artist per unique
   albumartist, merge with existing device albums, SendObjectPropList
   create / rewire update, `zune_link_artist` + per-track ArtistId,
   album art from the MD5 cache → statuses/progress/cancel throughout;
   200–500ms firmware breathing-room sleeps kept.
5. **Milestone B — all audio** via `zuuned_transcode_audio` on the
   transcode pool (0.9 progress split, temp cleanup, network-path
   local copy w/ timeout).
6. **Hardening**: single global progress model (fix mac's per-phase
   rebasing), post-sync status normalization (failed items
   re-queueable — fixes mac Known Issue), `zunetool torture` run.
7. **Format sniffing** (Phase-2 finding): pick retag-vs-transcode by
   MAGIC BYTES, not extension — this library has Zune-era WMA files
   named .mp3; the mac's extension check sends those through retag and
   emits garbage. Also add an ASF-header guard to id3_rewrite.

**Gate (hardware):** queue a local album → sync to Zune 30 → artist
displays correctly on device (verifies the 0x9808 fix), art on album,
cancel mid-file aborts cleanly, eject finalizes and device re-indexes.

---

## Phase 6 — Video Section (library side)

*Mac sources: `VideoMatcher`, `TMDBService`, `FanartTVService`,
`LibraryVideosView`, `VideoBrowserModel`, detail views.*

1. **TMDBService + FanartTVService (C++)**: QNetworkAccessManager,
   throttles, `vidthumbs` cache, keys from Settings w/ bundled
   fallback (decide packaging of bundled keys).
2. **VideoMatcher (C++)**: own SQLite side-connection; squash/tokenize
   /scoreCandidate; **folder-truth series memo** (one TMDB search per
   show); absolute-numbering anime mapping; batched writes; backoff
   60s→24h, 8 attempts.
3. **VideoBrowserModel (C++)**: memoized groupings off-thread,
   `seriesFullyOnDevice`, on-device keys.
4. **LibraryVideosView**: Movies / TV / **Anime** / **Needs Match**
   pivots; 180pt fixed cells; cards appear when `tmdbCached`;
   MovieCardView/SeriesCardView + OnDeviceBadge +
   ScaledPosterTitleOverlay; Needs-Match roll-up + bulk TMDB assign +
   inline lookup sheet.
5. **Detail views**: MovieDetailView (340pt glass sidebar, versions
   list, watched toggle), SeriesDetailView (season selector,
   EpisodeStillView rows w/ frame-grab fallback via
   FrameThumbnailService, queue-all), DetailViewPoster,
   PosterPickerSheet, EditEpisodeSheet.
6. **Settings Sync tab**: poster source, re-lookup buttons + counts,
   force refresh.
7. **Video sync** (SyncManager Phase 4): profile from Settings w/
   auto-detect from connected device family; probe → transcode with
   progress + one retry; metagenre routing; "Series - S02E05 - Title"
   FAT32 names; poster JPEG + UCS-2 description; TV vendor props
   (0xDA9A/0xDAB5/0xDAB6).

**Gate (hardware):** a real show folder matches via TMDB with posters;
sync one episode profile-2 to a device; **profile 0 (WMV) to the
Zune 30 — the Linux-first feature the MPVKit fork never shipped.**

> **Status 2026-08-28:** software side complete (scan → folder-truth
> matcher → browser/detail views → video sync phase w/ auto profile).
> Verified headless + on screen: live TMDB matching (Breaking Bad /
> Bob's Burgers / Die Hard, posters + stills), poster grid and series
> detail rendered, real 24-min episode → profile-0 WMV2 320x240 off
> the kubeplex mount. **Awaiting the device half of the gate** — plug
> in the Zune 30, add kubeplex Movies/TV/Anime as watch folders in
> Settings, queue an episode, sync.

---

## Phase 7 — Video Player

1. **MpvVideoItem**: `QQuickFramebufferObject` + `mpv_render_context`
   (OpenGL) — the canonical mpv/Qt Quick pattern; verify on Hyprland
   incl. hwdec (VAAPI).
2. **VideoPlayerService port**: video queue (series binge),
   onNextEpisode/onQueueFinished, EOF save, 90% watched rule.
3. **VideoPlayerView**: fullscreen overlay (stable identity, zIndex
   100): scrims, mouse-move controls with 1.5s auto-hide (views stay
   mounted; cursor hides), click/double-click, keyboard (space, ±10s/
   shift 60s, volume, M, F, Esc); bottom-right assembly = 400px vinyl
   + CurvedText title + **OrbitalControls** (transport arc, volume
   arc, mute) + audio/subtitle picker popups + fullscreen/close chips;
   chapter fallback ±60s; resume-position rules (>60s in, not last
   30s); music player auto-pause/resume handoff.

**Gate:** watch a synced episode fullscreen with chapter rings and
orbital controls on Hyprland; resume works; music ducks correctly.

**✅ DONE 2026-08-29** — plus beyond-plan extras: a second "disc"
controls style (faithful port of zuuned-web's player disc, selectable
in Settings → Appearance) and the episode ribbon (music queue ribbon
adapted for binge queues — jump episodes without leaving the player).
See AGENTS.md Phase 7 notes for the load-bearing implementation
details (flip, scrub bracket, EOF handling).

---

## Phase 8 — Photos (both directions)

**Custom album extension, 2026-09-27:** schema v7 and the native Photos page
now implement virtual albums and nested subalbums, separately from the folder
browser below. See [`PHOTO_ALBUMS.md`](PHOTO_ALBUMS.md) for persistence,
membership, deletion and transfer semantics and [`FEATURE_COMPLETION.md`](FEATURE_COMPLETION.md)
for the current verification status. Local organization does not require USB;
the original hardware round-trip gate remains separate. The September 28
development AppImage passes packaging, actual parent/child builder creation,
restart persistence and copied v5→v7 profile migration/normal shutdown. Exact
artifact and limits: [`TESTER_PHOTOS_2026-09-28.md`](releases/TESTER_PHOTOS_2026-09-28.md).

1. **DevicePicturesView full port**: parentId folder albums with
   device names + rename override, album cards, photo grid on the
   thumb cache (Phase 1.8), batch delete, copy-to-library.
2. **LibraryPhotosView**: folder-albums (id = path), stacked-cover
   cards, multi-select batch delete, album rename (mv + SQL rewrite),
   drill-down grid, ThumbnailCache → QQuickImageProvider with
   sourceSize decoding.
3. **Photo sync** (SyncManager Phase 3): 480px JPEG prep (libav path),
   `doSendPhoto` with album names, addPhotoAlbum folder scan.

**Gate (hardware):** photo album round-trips: library → Zune (visible
on device screen) and Zune → library.

---

## Phase 9 — Playlists (the "if we can figure it out" phase)

1. **Enumeration investigation** (libzune, on-branch): ZMDB scan
   reports 0 playlists on Keel + `get_object_info 0x2002` on playlist
   objects — the mac reads playlists via MTP enumeration
   (`zune_get_playlists`); reconcile on Linux hardware, wire-capture
   against the Windows client if needed (WIRE_CAPTURE_FINDINGS.md
   methodology). This is protocol work; everything else here is easy.
2. **PlaylistsView port**: device playlist list + detail (ids resolved
   against device tracks), delete, create sheet (name + device-track
   multi-select → `syncQueue.addPlaylist`).
3. **Playlist sync** (SyncManager Phase 5): `forge_playlist` on sync;
   save-queue-as-playlist from the ribbon.
4. Stretch (fixes a mac limitation): map queued *library* tracks to
   their post-sync device ids so fresh tracks can be playlisted in the
   same sync.

**Gate (hardware):** create a playlist on Linux, play it on the Zune.

---

## Phase 10 — Ship

**Tester preparation, 2026-09-08:** release work is tracked in
[`TESTER_RELEASE.md`](TESTER_RELEASE.md). The native build and isolated import,
migration, probe, packaging-helper and USB-installer checks pass. Schema v6
repairs music tags conservatively, preserves successful metadata on read errors,
and retains disc order. Packaging declares the actual Qt 6.8 floor, checks QML
dependencies, bundles `ffmpeg`/`ffprobe`, records source revisions, and installs
ordered canonical USB rules. The clean Debian source build and development
AppImage runtime/profile-upgrade rehearsal pass; packaging also checks every
ELF dependency and pins its launcher runtime. The final clean `6dbf632` AppImage
passes that runtime gate and opens on the Omarchy host. Draco 120 GB hardware
passes MP3 readback, ordered playlist, 20/20 repeated sends and video metadata
checks with verified cleanup. Physical playback, packaged photo sync and
first-install USB/picker checks remain; this phase is not yet marked complete.
The September 9 `9a6eeb6` diagnostics candidate adds bounded local session logs
and a Health report export (72 core and 34 report/QML checks). Its clean Debian
AppImage passes the expanded runtime gate, including the actual Health save
dialog, private native session logs and a copied v5 profile upgrade. See
[`TESTER_R2_DIAGNOSTICS.md`](releases/TESTER_R2_DIAGNOSTICS.md) for its checksum
and evidence. The hands-on checks above remain open.
The subsequent `87b2ecd` AppImage restores native desktop pickers by bundling
the matching Qt portal plugin and using the desktop's theme. Its actual GTK
portal folder/import/save requests pass, along with diagnostics and profile
upgrade checks. This closes the scripted native-picker gap; other desktops and
the remaining physical checks still need their own evidence. See
[`TESTER_R2_DESKTOP_DIALOGS.md`](releases/TESTER_R2_DESKTOP_DIALOGS.md).

1. **Settings About tab**: library stats + rescan, device stats +
   wipe-device (deleteObject sweep w/ progress), attribution logos,
   reset-library (delete DB+caches, clear onboarding,
   `QProcess::startDetached` relaunch).
2. **API key packaging**: bundled TMDB/Fanart/AcoustID fallback keys →
   compile-time constants or first-run prompt; document choice.
3. **Packaging**: Flatpak manifest (pins Qt/FFmpeg/mpv; udev
   first-run helper screen), AUR PKGBUILD (installs udev rule),
   optional .deb/.rpm via Gitea Actions.
4. **Regression**: side-by-side mac parity checklist; `zunetool
   torture`; fresh-clone build test on a clean container (Ubuntu) to
   validate the README.

---

## Dependency graph (why this order)

```
P1 Shell/Feel ──────────────┐ (independent)
P2 Transcode C ──┬──────────┤
                 │          ▼
P3 Library ──────┼──► P4 Music Player ──► P5 Sync (A needs P3; B needs P2)
                 │                          │
                 └──► P6 Video Section ◄────┘ (video sync)
                            │
                            ▼
                      P7 Video Player
P8 Photos  (needs P3 photos + P5 worker slots)
P9 Playlists (needs P5; protocol work independent)
P10 Ship (needs all)
```

P1 and P2 can run in parallel. P5 Milestone A (MP3 sync) only needs
P3 — music sync can be working before any FFmpeg code compiles.

## Verification discipline

Every hardware gate runs against the real Zune 30 (and later a Zune HD
if available — profile 2/3 video, HD-only ZMDB fields). Protocol
regressions: `zunetool info/list/send/purge/torture` after any libzune
bump. UI parity: screenshot next to the mac app at each phase gate.

## Automatic music artwork — 2026-10-01 feature candidate

`feature/automatic-music-artwork` adds background discovery for setup and later
imports, with complete local-file fallback before online album lookup. Exact
artist/full-title matches or explicit provider identities use Cover Art Archive
and Fanart; custom/cache protection and conservative edition matching remain.
See `MUSIC_ARTWORK_DISCOVERY.md` for gates and limits. Native test candidate only;
interactive acceptance and a new packaged release remain separate.
