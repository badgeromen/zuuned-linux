# AGENTS.md — AI Assistant Guide for ZuunedLinux

## Project Overview

ZuunedLinux is a native Qt 6 / QML application for managing Microsoft Zune
media players on Linux. Sister project of ZuunedMac — both apps share
libzune, the C library carrying the reverse-engineered PTP/MTP/MTPZ stack.

No kernel driver. No webview, no Electron, no Python. libusb IS the driver
on Linux — one udev rule and the hardware is ours. Compiled C++ linking
libzune.a directly (zero FFI: it's C, we `#include "zune.h"`).

**Hardware-validated 2026-08-27:** full MTPZ auth, library read, track
send, and purge all proven against a real Zune 30 over libusb on this
stack.

## Build & Run

```bash
cmake -B build -G Ninja     # configures + wires libzune's Makefile
cmake --build build         # builds libzune.a, then the app
./build/zuuned
```

Qt 6.8 or newer is required, including Qt Quick Controls, Dialogs and
Qt5Compat GraphicalEffects. See `README.md` for the complete Arch and
Debian build dependencies; the app also links mpv, FFmpeg, LAME and SQLite.

CMake drives `make -C libzune` automatically — after C changes just
rebuild; no manual copy step (unlike ZuunedMac's `cp libzune.a ../lib/`).

## Shared library discipline (public source snapshot)

This public checkout vendors `libzune/` as ordinary files. The historical private
checkout used a submodule; the rules below describe that original workflow.
Do not run remote submodule updates in this public snapshot. Make shared protocol
changes in `badgeromen/libzune`, then integrate a reviewed revision here with tests.
See `BUILDING.md` for the public native build and packaging limitations.

### Historical private submodule workflow

These rules exist because each was violated once and each violation cost
real debugging time (2026-08-27):

1. **libzune is pinned to `master`** — the wire-protocol line merged
   there 2026-09-02 (ZLP framing, autopsy codes, retry triage,
   mid-transfer abort, OpenSession auto-reset, playlist forge). The
   branch is recorded in `.gitmodules`; update with
   `git submodule update --remote libzune`, then commit the new pin.
2. **Never commit build artifacts** (`obj/`, `*.a`, `*.so`) — in libzune
   or here. Committed macOS `.o` files broke Linux three ways: `ar`
   choked on the Mach-O archive, the linker rejected stale objects, and
   16 "modified" files blocked branch switches. `.gitignore` now covers
   them; keep it that way.
3. **Linker says "file format not recognized"?** Stale Mach-O objects
   from a mac checkout: `make -C libzune clean`, rebuild.
4. **Pushing libzune changes:** commit on `master` (or a new branch +
   PR), push, then bump the pin here. Both apps consume libzune —
   protocol fixes belong in the submodule, never patched app-side.

## Key Architecture

```
┌──────────────────────────────────────┐
│ QML UI (qml/)                        │
│ Main.qml · Theme.qml design tokens   │
└──────────┬───────────────────────────┘
           │ Qt properties / signals
┌──────────┴───────────────────────────┐
│ DeviceService (C++ QML singleton)    │
│ = MTPService.swift of this app       │
│ libzune calls off-thread             │
└──────────┬───────────────────────────┘
           │ #include "zune.h" (plain C)
┌──────────┴───────────────────────────┐
│ libzune (static .a, submodule)       │
│ Zune Rebellion API                   │
│ breach, smuggle, forge, purge, etc.  │
├──────────────────────────────────────┤
│ Native PTP/MTP/MTPZ Stack            │
│ Split header/data · ZLP rule         │
│ SendObjectPropList (0x9808)          │
└──────────┬───────────────────────────┘
           │ usb_libusb.c backend
┌──────────┴───────────────────────────┐
│ libusb-1.0 (userspace — no driver)   │
│ detach kernel driver, claim iface 0  │
└──────────┬───────────────────────────┘
           │ USB bulk pipes
┌──────────┴───────────────────────────┐
│ Zune Hardware                        │
│ VID=0x045E PID=0x0710/063E           │
└──────────────────────────────────────┘
```

## Directory Structure

```
zuunedlinux/
├── CMakeLists.txt          Build config (drives libzune's Makefile too)
├── src/
│   ├── main.cpp            Entry point
│   └── DeviceService.*     libzune bridge — ALL device calls go here
├── qml/
│   ├── Main.qml            App shell
│   ├── Theme.qml           Design tokens (ported from ZuunedMac Theme/)
│   └── fonts/              Permanent Marker, Specimen Alien (bundled)
├── packaging/
│   └── 68-zuuned.rules     installed as both 68- and 72- rules
└── libzune/                included C library source
```

## Code Conventions

### C++
- C++20, Qt idioms. `QML_ELEMENT`/`QML_SINGLETON` registration, not
  manual `qmlRegisterType`.
- **All libzune calls go through DeviceService**, and always off the UI
  thread (QtConcurrent + QFutureWatcher). libzune is synchronous C — one
  device operation at a time, same discipline the `actor` gave ZuunedMac.
- Results land on the UI thread via signals; QML reads reactive
  properties only.

### QML
- **Every color, spacing, radius, and duration references `Theme.qml`.**
  Never raw values in views — same token names as the Swift source
  (`ZuneColors.swift`/`ZuneStyles.swift`). When the mac theme changes,
  port the token, not the usage sites.
- QML is the native scene graph, not a webview. No JS libraries, no
  network in the UI layer.

### C (libzune)
- C99, Zune Rebellion naming (breach/smuggle/forge/purge…).
- See `libzune/CLAUDE.md` for full conventions and `libzune/docs/TOC.md`
  before searching the filesystem.

## Device Testing

### zunetool — test protocol changes WITHOUT the app

```bash
cd libzune && make zunetool
./zunetool info              # breach + MTPZ auth + device info
./zunetool list              # library listing
./zunetool send <mp3> [title] [artist] [album] [genre]
./zunetool purge <item_id>
./zunetool torture <file>    # 20× send — flushes ZLP edge cases
```

Every failure prints `zune_autopsy()` — the device's real PTP response
code. `0x2005` = refused on purpose (don't retry), `0x200C` = store
full, `0x02FF` = transport wounded (reset/unjam territory).

### USB access
- `packaging/68-zuuned.rules` is installed as both `68-zuuned.rules` and
  `72-zuuned.rules`: suppress libmtp probing before 69, reapply exclusions
  before seat-late ACLs at 73. The app embeds the same canonical bytes.
  See `packaging/TESTER_INSTALL.md`; never restore a lone 45/99 rule.
- Dev box note: a homegrown `zune_usb` DKMS module (author BadgerOmen)
  may already claim the device and provide open node perms. libusb
  detaches it automatically on open; it does not conflict.

### Hardware quirks (learned on a real Zune 30, Keel family)
- **OpenSession wedge:** after ~2 breach/sever cycles the device may
  stop answering OpenSession (bulk IN timeout, autopsy 0x02FF).
  `zune_breach()` auto-resets and retries once. Manual fallback:
  `usbreset 045e:0710`.
- **ZLP rule:** in split header/data mode the zero-length packet decision
  uses PAYLOAD length, not container length (`ptp.c`). Wire-verified
  against the Windows 8 client. Don't "simplify" it back.
- **Finalize on Keel:** vendor ops 0x9201/0x9202 return 0x2005
  OperationNotSupported — expected, not a bug. CleanDataStore (0x9108)
  is what triggers re-index.
- **4 GiB cap:** MTP ObjectCompressedSize is 32-bit; libzune refuses
  larger files rather than silently truncating.
- New content may not appear in `list` until the device re-indexes.

## Customize sheet

**Connection rule (user decisions, 2026-09-06 and 2026-09-27):** every addition
to the Zune transfer queue requires a connected Zune. Media drags require a
connection except local photos dragged into the photo-album builder: that
organization workflow is explicitly offline. Local photo drags expose a
separate target key; device drops recheck connection. Opening the music
playlist builder is not an exception. Playback queues, metadata/artwork edits,
and playlist creation/membership/order edits work without a device. Recheck
connection when an action/drop/picker completes; SyncEngine enforces the same
rule at its add boundary. Existing saved transfer queues survive disconnects.
The action audit and native verification gates are documented in
`docs/ACTION_AUDIT_FIXES.md`.
Queue remove targets, the custom ZUUNED badge, device-row library membership,
and the bottom notification strip are covered by `docs/QUEUE_AND_FEEDBACK.md`.
Video transfer titles, filename/readback separation, existing-device title
repair, and silent gallery scans are covered by `docs/VIDEO_TITLES.md`. Never
use an episode title alone for device dedup or interrupted-send recovery.

**Customize / Sleeve (2026-09-06):** the user-approved native redesign is on
`ux/6-customize-design`. Read `docs/SLEEVE_CUSTOMIZE.md` before changing it.
Artwork and identity are one staged draft; browsing must never commit. Apply
prepares resources off-thread and reports actual save completion. Video schema
v3 separates `custom_poster` from manual identity so automatic matching can
preserve art without freezing unmatched items. Keep request correlation and
the automatic-matcher write guards intact. Music identity is manual; music
reset releases artwork only. Future mockups default to wireframes unless the
user asks for a richer study.

## Known Issues

The active safety/identity cleanup and its verification gates are tracked in
`docs/CODEBASE_CLEANUP.md`. Device disc/track availability varies; software
matching tests do not certify physical readback. Hardware photo transfer and
host audio/GPU/USB release gates remain separate. Classic per-media byte
discovery is deferred, not an active release blocker.

Historical rendering issues were resolved in August: stale OpacityMask content,
per-pixel QML mask generation, and uncapped mask bite depth. Preserve the native
GrungeMaskProvider and the rim cap.

## Debugging

- App/libzune logs: stderr with `[prefix]` tags (`[ptp]`, `[mtpz]`,
  `[libusb]`, `[libzune]`)
- USB enumeration: `lsusb -d 045e:` · `journalctl -k | grep -i usb`
- Wedged device: `usbreset 045e:0710`
- Desktop interference: gvfs-mtp may probe the device — the udev rule's
  `MTP_NO_PROBE` handles it; check `pgrep -a gvfs` if something steals it.

## Roadmap

**2026-09-27 feature scope:** read `docs/FEATURE_COMPLETION.md` for the active
phased backlog: custom/nested photo albums first, then disc-aware device dedup,
on-device metadata/artwork editing and compatible hardware video encoding.
Device-browser video playback is explicitly out of scope (save-to-library
remains). Classic per-media storage byte discovery is deferred, potentially
requiring reverse engineering; do not treat it as an active release blocker.

**Custom photos, 2026-09-27:** schema v7 stores virtual photo albums, nested
parents and ordered many-to-many membership. `docs/PHOTO_ALBUMS.md` documents
the semantics. Organization is offline; album deletion preserves photo files
and library rows. Transfers select direct members only, never silently flatten
subalbums. Keep the backed-up migration, non-reused photo/album IDs, cycle/depth
validation, atomic create-with-members and stale-reorder guards. Software gates
live in `tests/photo-albums/` and `tests/actions/photo-run.sh`; packaged/device
photo presentation remains a separate release gate.

**Photo builder, 2026-09-27:** Photos New/Edit opens a floating island beside
the sidebar, matching mixtape composition. `PhotoTrayState` is an independent
staged draft; bulk selection survives folder navigation and local photo drags
work offline. While composing, photo plus buttons feed the draft. Save compares
the original album snapshot and atomically commits name/parent/member order;
stale saves keep the draft and fail truthfully. Dirty replacement/cancel requires
discard confirmation. Keep selection order, bounded GridView/ListView delegates,
picker-generation guards and device-drop connection checks. Gates: 73 native
service, 22 photo action and 10 builder checks; photo/builder suites also pass
X11/OpenGL, including a narrow 5,000-photo draft. Existing playlist actions pass.

**The authoritative plan is `docs/PORT_PLAN.md`** — a 10-phase sequence
covering the full mac-app port (shell/feel → transcode C → local
library + onboarding → music player → sync engine → video section →
video player → photos → playlists → packaging), each phase with a
hardware/verification gate. Read it before starting any feature work;
update its phase status and this file when a gate passes.

Done so far: protocol stack (hardware-proven), app chrome + grunge/spray
visual identity, device browsing with album art, hotplug auto-connect,
DeviceWorker serialization. **Phase 1 (Shell & Feel) complete**: splash,
device-presence bloom + genre mood color, toast host, panel slide motion,
settings shell with live Appearance tab + wallpaper layer, AppSettings
singleton (all mac keys), connection staging, device photo thumbnails.
**Phases 3-5 complete**: local library (SQLite/scanner/onboarding/
kubeplex CIFS mount), music player (mpv core, vinyl, ribbon, mini
player), sync engine — HARDWARE-GATED 2026-08-28: album synced from
FLAC with correct album/artist objects on the Zune 30. Critical libzune
finds: zero-length ZLP guard AND zero-byte abstract-object commit (the
1-byte hack contradicted the declared size → 0x2002 on every album
forge; BACKPORT TO MAC). Device deletion, on-device badges live.
**Phase 2 (transcode C layer) complete**: transcode/ builds vs system
FFmpeg 9 + LAME (`zuuned_transcode` + `transcodetool`); profile 0 =
wmv2+wmav2 ASF verified (Zune 30 video, no forked FFmpeg); libzune
probes in-process (USE_LIBAV) on Linux. LAME superframe chunking bug
fixed here — backport to the mac. Sync must sniff magic bytes, not
extensions (WMA-as-.mp3 files exist in real libraries).
**Phase 6 (video section) software-complete 2026-08-28**: video scan
(folder-truth series, skip rules, deferred probing), VideoNaming
(17-case self-test: `librarytool names`), TMDB/Fanart clients +
VideoMatcher on its own thread (series memo, absolute-anime mapping,
60s→24h backoff), VideoBrowserModel + LibraryVideosView (Movies/TV/
Anime/Needs-Match, poster cards, detail pages, bulk fix-match sheet),
FrameThumbnailService, video sync phase (transcode w/ retry → FAT32
wire names → smuggle movie/episode/clip/other w/ poster + description),
Settings Sync tab live (auto profile default: Zune 30 → WMV).
`librarytool videoscan <folder> <tmpdb> [movies|tv|anime]` is the
headless gate — PASSED vs live TMDB. Hardware gate PASSED 2026-08-28:
48-episode season → Zune 30 as profile-0 WMV, 0 failures. Video
PLAYBACK is Phase 7 — play buttons toast until then.

**Phase 7 (video player) — DONE 2026-08-29, eyeball-gated live.**
MpvVideoItem (QQuickFramebufferObject + mpv_render_context, OpenGL RHI
pinned in main; FLIP_Y=0 and NO setMirrorVertically — that double-
flips), VideoPlayerService (binge queue mirrored into mpv's playlist,
EOF auto-advance syncs index only, 90%-watched via saveVideoPosition,
5s position saves, music ducks on video start), VideoPlayerView
overlay (vinyl OR single-ring "disc" style — Settings → Appearance;
episode ribbon top-center on binge queues, jumpTo on card click; track
pickers anchored above their chips). Scrub bracket: beginScrub pauses,
150ms keyframe scrubs, endScrubMs exact-seek + resume — seeking while
PLAYING audibly loops the same keyframe. Disc style is a faithful port
of zuuned-web's Tg component (extracted from the minified bundle on
zw.badgeromen.xyz; geometry scales by size/200): band centered in the
poster→rim annulus, smooth 200-segment opaque gradient (translucent
segments seam at joints), rounded start cap, pink head just proud of
the band. Volume arc/mute layout is the web original — three attempts
to "improve" the mute chip all read worse; leave it.
DeviceVideosPage playback is explicitly out of scope (2026-09-27); device
videos can be saved to the library. Full-orbital vinyl uses real mpv chapters.

**Sync performance (2026-08-28):** three-layer speedup, all
hardware-gated. (1) NVDEC (CUDA) decode in the transcode layer —
22min 1080p x265 → WMV in ~38s vs ~2.5-3min SW (~4x); automatic SW
fallback; Settings toggle 'hwDecode'. (2) Pipelined video sync: ≤2
transcodes run ahead of the STRICTLY-SERIAL one-at-a-time USB sends,
≤3 buffered temps; failed markers keep the in-order cursor moving.
(3) libzune 1MB bulk chunks (was 256KB): 15+ MB/s sustained vs ~7
(zunetool torture 20/20 on Keel); '[ptp] sent N MB …(MB/s)' telemetry
on payloads >8MB. Net: a 12-episode season ~35min → ~8-10min.

## Development Methodology

### Core Principles
1. **Full-scale solutions, not shortcuts** — root cause, not symptom.
2. **Think before coding** — read the docs and captures first; the
   protocol answers live in `libzune/docs/` (WIRE_CAPTURE_FINDINGS.md is
   ground truth from the Windows client).
3. **Document everything** — hardware findings go in libzune docs;
   app decisions go here.
4. **No external runtime dependencies** — system libs only, bundled at
   packaging time. Nothing downloaded at runtime.
5. **Theme parity is a feature** — this app should be unmistakably
   Zuuned next to the mac app. Tokens stay in sync.

### Agent Workflow (for non-trivial tasks)
1. **Architect (Claude + User)** — define problem, design solution, set goals
2. **Developer Agents** — full context, no TODOs or placeholders
3. **QA Agents** — validate changes; hardware claims require a real
   device test via zunetool, not "should work"

**Application license (user decision, 2026-10-01):** original Zuuned app code
is GPL-3.0-or-later; see `LICENSE` and `COPYING.md`. Preserve separate third-party
licenses. Source will be available elsewhere; keep release source links accurate.

**GitHub distribution (user decision, 2026-10-04):** `badgeromen/zuuned-linux`
hosts Linux application source AND releases. `badgeromen/libzune` hosts the shared
C library. This supersedes the earlier downloads-only repository decision for
`badgeromen/zuuned-linux`. Move public-facing links to the new account; do not alter
historical release evidence. Keep populated MTPZ data and private history out of
public source. Prepare clean histories and review author identities before push.

**MTPZ release behavior (user decision, 2026-10-03):** official downloads retain
embedded MTPZ authentication. Keep the populated header in private Gitea and
supply it to trusted release builds; omit its values from public source. Preserve
the external-file override. Public source preparation must not make release users
supply a separate authentication file. See `docs/RELEASE_ARCHITECTURE.md`.

**Git push (user decision, 2026-09-08):** always ask the user before every
push and wait for explicit approval. Earlier push approvals do not authorize
later pushes. Prepare and verify the local change before asking.

### Documentation
- **2026-10-03 public source preparation:** `tools/export-public-source.mjs`
  creates a fresh source snapshot without private history, the embedded MTPZ
  header or build artifacts. Approved provider API defaults remain. Private files stay
  in place. Read `docs/PUBLIC_SOURCE_EXPORT.md` before publication. A fresh push
  approval remains required; never mirror private Git history.
- **2026-10-02 local music copies:** schema v9 stores verified audio payload
  fingerprints for every indexed song, regardless of metadata completeness.
  Group by verified audio, preserving known full album and disc/track conflicts;
  incomplete metadata must never bridge ambiguous editions. Preserve raw source rows and
  playlist IDs; only the browser/model display list and counts are grouped.
  Existing playlist repetitions survive; new adds recognize equivalent source IDs.
  Hash work stays on the scanner worker, with stat guards and cancellation;
  never replace it with title-only or whole-file-size dedup. See
  `docs/LOCAL_MUSIC_COPIES.md` and `tests/local-music-copies/`.
- **2026-10-02 native music matching candidate:** read `docs/MUSIC_MATCHING_PLAN.md`
  before implementing the artist/album follow-up. Automatic discovery must not
  change tags, grouping or device dedup. Keep exact-release versus general-cover
  scope explicit, manual match persistence and local/custom art protection.
  October 2 correction: apply confidently matched general album covers
  automatically; only uncertain artist/album identity requires a match.
  Software gates pass (98 matching, 20 discovery, 7 persistence, 63 QML,
  actual Apply and regression suite). User acceptance/new packaging remain.
  Auxiliary `_artworkMatch`, `_artworkDiscovery`, `_artworkChoiceRevision` JSON
  fields are not metadata pins; preserve manual-save revision race protection.
- **2026-10-02 device artist reuse:** local libzune branch
  `fix/reuse-device-artists` makes forge_artist reuse a live Name match before
  creation. Failed inventory reads never justify creation. Existing duplicates
  remain; repeat-sync software gate is `libzune/tests/run-artist-reuse.sh`.
  Live Zune HD logs confirm reuse for Selena Gomez, Zweihänder and Post Malone
  (2026-10-02); existing-duplicate cleanup remains separate. See libzune/docs/
  ARTIST_REUSE.md for the Jelly Roll deletion/reindex caveat. Push libzune
  before staging a new pin.
- **2026-10-01 artist album evidence:** ambiguous/incomplete name searches can
  use up to three local album titles to corroborate exactly one candidate MBID.
  Reject conflicting credits and incomplete album responses; never replace a
  saved identity or fall back to an unrelated same-name artist. Context is
  ephemeral, not written into media tags. Provider fixture: 76 checks passed.
- **2026-10-01 automatic album artwork candidate:** `OnlineAlbumArtService`
  serializes missing-cover lookup after `AlbumArtService::requestSources`
  exhausts the complete local batch. Preserve exact artist/full-title or pinned
  provider matching, generation invalidation, Customize pause and atomic
  no-overwrite publication. Setup/imports share background discovery; Settings
  has Find missing artwork. Details/gates: `docs/MUSIC_ARTWORK_DISCOVERY.md`.
- **2026-10-01 multi-folder music artwork:** on-demand album covers try all
  known track paths, with per-source misses and serialized per-album fallback.
  Artist lookup failures get one delayed retry with identity/customization
  guards preserved. See `docs/MUSIC_ARTWORK_DISCOVERY.md` for tests and limits.
- **2026-10-01 display font:** Specimen Alien replaces Misdemeanor in app,
  previews and test resources. Legacy `headerFont=misdemeanor` resolves to
  `specimenAlien`; Permanent Marker remains the default. Source/coverage live
  in `tools/font-studio/`; preserve the separate original Specimen study.
  Font integration gates: settings persistence, QML bundled-family loading,
  and the Alien validator's declared 368-character Latin coverage.
- **2026-09-28 identity/recovery cleanup:** `MusicIdentity` is the shared policy
  for music dedup, badges, playlist linking and reverse imports. Ambiguous rows
  fail explicitly; missing disc/track metadata stays unknown. Schema v8 preserves
  pending playlist disc/track values with a backed-up migration. Prepared audio
  preserves source disc tags or an explicit positive override. Music/photo
  interrupted recovery requires an exact object ID, device serial, compatible
  live readback and a correlated completion; legacy/ID-less markers require
  manual review. Software gates and hardware limits: `docs/CODEBASE_CLEANUP.md`.
  Run `bash ci/check.sh` for the repeatable native regression suite.
- **2026-09-28 photo testing candidate:** the Debian AppImage now passes actual
  photo-folder import, staged parent/nested-child creation and restart
  persistence, native portal folder/open/save, diagnostics, and copied v5→v7
  profile migration with normal shutdown. Exact checksum and isolated evidence:
  `docs/releases/TESTER_PHOTOS_2026-09-28.md`. This is a labeled dirty development
  artifact; physical photo-transfer and host audio/GPU/USB gates remain separate.
- **2026-09-28 provider shutdown:** cancel `ArtworkHttp` on `aboutToQuit` and
  drain the global worker pool before destroying QML services or QApplication.
  Destroying a watcher does not cancel its QtConcurrent callable. Active replies
  must abort on their owning thread; MusicBrainz mutex/pacing/retry waits must
  observe shutdown too. The isolated stalled-request gate is
  `tests/provider-shutdown/run-backend.mjs` (6 checks; existing provider/cache
  suites also pass). This does not add cancellation to TMDB/scanning/transcode.
- **2026-09-09 device album-art import:** successful Zune music pulls preserve
  the separate representative-sample JPEG carried by the abstract album
  object. The image is normalized into the local album cache after extraction
  and never replaces local/custom artwork. `ZuneDBArtist` exposes only ID and
  name, so artist portraits remain online/custom with the flat fallback. The
  focused gate is `tests/device-music-import/run.sh`.
- **2026-09-09 desktop file pickers:** the user requires the desktop's native
  chooser and its own theme for every open/save/folder action. Keep the standard
  QtQuick.Dialogs API, bundle `platformthemes/libqxdgdesktopportal.so` built for
  the AppImage's Qt, and select it in AppRun. Native installs default to the
  portal when no platform theme is configured. Do not restore the `GTK_THEME`
  force or ship a replacement file manager. Packaging must verify the plugin;
  a runtime gate must prove real portal folder/open/save behavior, not merely
  click Qt's fallback dialog. The desktop supplies its FileChooser backend.
  The `87b2ecd` AppImage passes actual GTK portal folder/open/save selections,
  correlated by app/backend PIDs and returned URIs; diagnostics and copied v5
  upgrade also pass. See `docs/releases/TESTER_R2_DESKTOP_DIALOGS.md` for the
  exact artifact/harness and host-desktop limits.
- **2026-09-09 tester diagnostics:** automatic native stderr/Qt session logs
  start before GUI initialization and stop after app/worker teardown. Keep
  `--version` side-effect-free. Logs retain at most eight 2 MiB files, redact
  credentials/authentication dumps before persistence, and stay owner-only.
  Health exports a local, reviewable `.txt` report with allowlisted system/app
  details and bounded recent logs, off-thread with truthful completion. Never
  include a full environment, settings, database or automatic upload. Preserve
  snapshot reads outside the capture mutex and the process ownership lock.
  Native gate: 72 core checks (also ASan/UBSan), 34 report/QML checks. Read
  `docs/DIAGNOSTICS.md`. The clean `9a6eeb6` AppImage also passes actual Health
  export, private session-log lifecycle and fresh/copied-profile runtime gates;
  exact evidence is in `docs/releases/TESTER_R2_DIAGNOSTICS.md`. Physical device
  playback and the remaining release checks are still separate.
- **2026-09-08 tester preparation:** schema v6 stores disc numbers and
  successful probe fingerprints. Native libzune metadata merges container and
  selected-stream tags (Ogg/Opus included), with additive disc/year fields;
  consumers must rebuild. Failed probes preserve good rows and retry later;
  automatic repair preserves manual edits and collection art/identity pins.
  Album playback and transfer queues retain disc/track order. Migration backup
  uses SQLite's backup API so committed WAL pages survive. Native import checks
  (33), native/fallback probe checks (64 each, sanitizers), migration checks (19)
  and QML music checks (23) pass. See `tests/music-import/README.md`.
  Packaging now checks its Qt/QML dependencies, bundles FFmpeg executables,
  identifies app/libzune builds, and verifies canonical USB installation.
  `docs/TESTER_RELEASE.md` tracks candidate, clean-system and hardware gates;
  these are separate from a successful developer build.
  **Packaging compatibility:** QML resources are aliased into the `Zuuned`
  module root with `NO_GENERATE_EXTRA_QMLDIRS`; preserve relative asset paths.
  The former subdirectory redirect produced an ambiguous self-import on Debian
  Qt 6.8.2. Both 6.8.2 and the native 6.11.2 build now pass. AppImage inventory
  checks every bundled ELF/plugin dependency against a narrow host allowlist;
  linuxdeploy's broad blacklist alone is insufficient. Tools and the AppImage
  launcher runtime are checksum-pinned. The container rehearsal uses no USB or
  network, disposable XDG profiles and an optional read-only copied v5 profile;
  this never substitutes for host hardware/audio/GPU checks.
- **2026-09-08 Artwork print Appearance integration:** the main app now has
  Original / Clean ink / Halftone / Worn print choices, a live local sample,
  collapsed fine-tuning and per-look persistent reset. Original is the default;
  accepted print presets are Detail 100, Halftone strength 14 / Dot size 17 /
  color, Worn wear 14. `ArtworkPipeline` + `ArtworkRequest` share native local
  processing and cache work; `ArtworkImage.qml` routes selected local artwork
  on album/artist/poster/player/queue/genre/mixtape/Customize surfaces. Keep its
  public source URL original: metadata, artwork choices and device transfers
  never use derived print bytes. Photos, wallpapers, app marks and remote
  provider candidates retain original rendering. No Zune is required.
  Gate passed: native build, 58 pipeline checks, 12 OpenGL display passes,
  settings persistence/interactions and 117 existing surface/action checks.
  The app opens on Settings and the actual Appearance capture is warning-free.
  Keep image opacity switching for hidden mask sources and the displayed(url)
  acknowledgment that protects both current and loading PNGs from eviction.
  Read `docs/ARTWORK_STYLE_STUDY.md` for the approved defaults and gate.
- **2026-09-07 local print proof:** `src/artwork/PrintRenderer` now provides
  deterministic CleanInk/Halftone/WornPrint processing. The separate native tool
  in `tools/artwork-preview/` reads cached artwork and opens live comparisons;
  it remains a developer proof alongside the main-app integration above.
  Original image bytes, alpha, stale-worker publication and cache pruning have isolated tests under
  `tests/artwork-print/`. Read the tool README before expanding this pipeline.
  **2026-09-08 wear reference:** the user's worn VHS carton calls for chipped
  edges plus rubbed coating and sparse surface scratches/creases. Wear is seeded
  by image content: different artwork gets different damage, stable across
  controls/reopening. A border alone or dense noise across faces misses the brief.
  Surface scuffs use warped gradient noise and directional rubbing; do not
  restore the square-cell thresholding that made damage look like QR patterns.
  **Comic Halftone:** the preview has a Halftone-only Dot size slider and
  color/black-and-white choices. These controls must not alter or invalidate
  Original/Clean/Worn variants; keep their worker correlation and cache tests.
- **2026-09-07 artwork study and parsing audit:** optional print treatments are
  explored in `docs/ARTWORK_STYLE_STUDY.md` and `mockups/artwork/`; these are
  generated concepts whose provenance remains separate from the implemented
  native filters and Appearance integration. The music/video reports under
  `docs/audits/` distinguish native reproductions from code-path findings and
  preserve diagnostics. Consult them before scanner/matcher fixes or a rewrite.
- **2026-09-07 provider correction:** Fanart v3's HTTP200 `{}` means no artwork.
  Same-name artist browsing preserves successful portraits despite absent/failed
  siblings. MusicBrainz identity and album-art searches share cached requests,
  temporary-failure cooldowns and bounded stale positive fallback. See the
  provider outage gates in `tests/music-identity/README.md`.
- **2026-09-07 library completion:** Sleeve Customize now matches album/artist
  identities online and stores genre/mixtape artwork. MusicBrainz/Fanart/Deezer
  remain native, cached worker services; artist choices use stable provider IDs.
  Playlist imports reserve ordered nullable slots (schema v4); collection
  overrides use schema v5. See `docs/SLEEVE_CUSTOMIZE.md`,
  `tests/playlist-order/README.md`, and `tests/music-identity/README.md`.
  Virtual/nested photo albums were implemented September 27; expanded device
  editing remains upcoming. Local collection editing never requires a Zune.
- `libzune/docs/TOC.md` — exhaustive function index
- `libzune/docs/LINUX_TESTING.md` — the Linux rig this app grew from
- `libzune/docs/WIRE_CAPTURE_FINDINGS.md` — wire-protocol ground truth
- `README.md` — user-facing build + udev setup

### Restarting the app (HARD RULE)
On this Omarchy development desktop, read and use the Omarchy skill for window,
launch and capture operations (user preference, 2026-09-08). Prefer its command
helpers; do not guess older Hyprland dispatcher syntax.

NEVER kill/restart the running app without first checking the current
app log for an in-flight sync (look for a send in progress with no
matching done line). Killing the process mid-`SendObject` aborts the
USB session with the object's DB row already created on the device —
the Zune is left with a truncated media entry that CRASHES THE
FIRMWARE on playback (device restarts when playing). Happened
2026-08-29 with a 2.65GB movie mid-wire. Recovery: delete the partial
object off the device, then clean Eject to trigger re-index.
