# AGENTS.md — AI Assistant Guide for ZuunedLinux

**Local build handoff (user decision, 2026-10-13):** after a completed
user-facing local build, restart only Zuuned with the fresh executable, preserving
the current profile and workspace. Do not interrupt an active device sync; defer
the restart until it finishes. Never close the terminal, Codex or desktop session.

**2026-10-13 compact playback UI:** windows below the existing 1100-logical-pixel
headroom breakpoint use Now Playing instead of the top ribbon, as do narrow
windows. Resizing an existing queue opens the island unless a builder is active;
closing it persists through track advances. The island has a fixed top-right X
that turns red on hover/press/focus, with no background, and focus-scoped Escape dismissal. Verification and desktop
limits are tracked in `docs/PLAYER_LAYOUT_AUDIT_2026-10-12.md` and
`tests/player-layout/README.md`.

**Scope update:** podcasts and audiobooks are active; defer HD artist backgrounds
until both work. Use the dedicated `feature/podcasts-audiobooks` branches.

**Active private workstream, 2026-10-08:** read
`docs/SPOKEN_MEDIA_AND_DEVICE_ART.md` before podcast, audiobook or Zune HD artist
background changes. Both repositories use `feature/podcasts-audiobooks`,
based on the private visualizer checkpoint. Device protocols belong in libzune;
feeds, downloads, local persistence and provider art selection belong in native
app services. Preserve serialized device ownership. Advertised descriptors are
not hardware write validation; keep category, resume and artwork gates separate.
The initial `zunetool media-capabilities` probe is read-only after connection.
Experimental podcast C APIs now have 50 software checks. After a physical reconnect,
the Keel gate passes isolated series/MP3 creation, repeat reuse, byte-for-byte
extraction and cleanup with the original wildcard inventory restored. This does
not prove playback, resume or HD support. The subsequent real Moonbound trailer
transfer passed repeat reuse and byte readback; the user confirmed podcast
category presentation on the Classic. That initial trailer had no artwork. The
October 9 app transfer sends show artwork. The user subsequently confirmed
podcast audio playback and images work on the Classic. Firmware resume and
other models remain separate gates.
Counted UTF-16 text requires a terminal NUL; preserve the documented Keel
filename/schema/timestamp readback semantics and successful-auth write guard.
`src/spoken/` now includes the native RSS parser, local audiobook probe,
serial service and separate `spoken/spoken.db` schema 1. The first native pages
support RSS preview/subscription/downloads, ordered local book imports, chapters,
custom covers and local playback/resume/bookmarks. Customize is right-click only;
podcast covers reuse split + / arrow; books use one whole-book +. Podcast transfer and drags now use
typed SyncEngine/DeviceWorker admission
and recovery. Audiobooks now have dedicated typed admission and sends, book/part
drags and explicit selection. Small multipart MP3 protocol gates pass on Classic
and HD. Chaptered sources now prepare ordered device parts through native PCM
range encoding; the complete source timeline is preserved. Local image drops work offline. Do not use music identities for spoken media.
Gates: `tests/spoken-media/run.sh` and `tests/spoken-media/native/run.sh`; the
latter uses real native services/QML/mpv, isolated profiles, loopback HTTP and no
USB. See the dated audiobook hardware evidence and remaining resume/chapter
gates in the spoken-media plan.
This work stays on private Gitea until approved for wider distribution. Existing
per-push approval and visualizer/public-export restrictions remain in force.

**2026-10-09 spoken UI correction:** playback belongs in the existing top ribbon
and now-playing island, never a separate bottom strip. Both use spoken cover IDs,
mic/book icons and skips/speed/bookmarks/sleep/chapter controls. Music modes stay
preserved but inactive for spoken queues; music speed resets to 1×. Settings →
Library offers reviewed native audiobook folder import (one book or one book per
immediate subfolder), with nested discs, natural order, atomic batch saves,
repeat reuse and overlap refusal. It is a manual importer, not a watch folder.
See the private plan and `tests/spoken-media/native/run.sh` for exact contracts.

**2026-10-09 podcast readback correction:** Pavo schema-0x10 podcast objects
normalize redundant SeriesName to empty and MetaGenre to zero, as Keel does.
libzune preserves exact series/source/title/size checks. Explicit
`ZUNE_PODCAST_FAILED_CLEANED` proves new-object deletion; the app retires recovery
only for that correlated outcome, never merely ID zero. Unknown writes retain
review. See the private spoken plan; HD playback and Classic retest remain gates.

**2026-10-09 device spoken browser:** the Zune island includes Podcasts and
Audiobooks with lazy serialized native inventory, series/folder-ID grouping,
covers, selection and confirmed removal. Keep request/serial/session guards,
live full-selection validation before deletion and successful-ID-only pruning.
Tests: `tests/device-spoken/` and libzune `tests/run-spoken-inventory.sh`.
No browsing action authorizes deletion; live deletion remains user-confirmed.

**2026-10-09 spoken organization:** local podcasts use Shows, Starred shows and
Starred episodes; audiobooks use Books, Libraries and Starred. Show/episode/book
favorites are independent and persist through refresh/import. Sublibraries are
ordered many-to-many virtual collections, never folders or transfer identities.
Use native revision-checked atomic saves/deletes; deleting a collection preserves
all books/files. Preserve shared GalleryView/header/breadcrumb styling, offline
organization, right-click Customize and whole-book queue actions. Gates:
`tests/spoken-organization/run-qml.sh` and `tests/spoken-media/native/run.sh`.

**2026-10-09 podcast retention:** optional `podcastAutoCleanup` defaults off.
Sync may run with an empty queue to clean firmware-marked-played podcasts.
Persist exact device-scoped listening history before deletion; then revalidate the
complete plan on the worker. Unknown state never authorizes deletion. Preserve
Keep on Zune independently of favorites, local files and other media. Remembered
consumed history suppresses re-addition only for the same device while enabled.
Podcast cover-arrow/show-drag selection offers newest/oldest count and manual
selection, with optional unplayed filter and per-show preferences. No background
queueing on feed refresh. See the spoken plan for tests and hardware limits.

**2026-10-10 podcast visibility:** local episodes expose played/played-on-Zune
and the existing PNG device badge from exact feed/source inventory. Cache status
per session, defer reads during device work, treat unavailable state as unknown,
and prune only confirmed deletion IDs. The show picker defaults to newest 10
unless a saved preference exists, with oldest/count/manual options preserved.
Cleanup actions appear as transient `podcast-removal` queue rows with truthful
progress. Never persist/replay these actions or count them as upload capacity.
See the private spoken plan for current contracts and verification.

**2026-10-10 cleanup preview correction:** removal actions must appear before
Sync, from correlated device source/played cache plus local retention choices.
Sync still revalidates the complete plan before deletion. Removing a preview
row/group or clearing the queue excludes those episodes for the current
connection; Keep on Zune is persistent protection. Previewing never writes
listening history or sends a delete. See the spoken plan for live Classic
preview evidence and the remaining actual-deletion gate.

**2026-10-10 starred podcast queue:** Settings → Library offers opt-in queueing
of newest unplayed episodes from starred shows on connection, with 5/10/custom
count and saved per-show count overrides. Native `PodcastQueuePlanner` owns RSS
refresh/selection; + honors saved selection and accepts feed-only entries.
Existing device episodes count toward N before upload filtering. Preserve exact
identity, unknown-inventory deferral, connection generation guards, pending-edit
ordering and one-pass behavior. Clear queue cancels in-flight additions; transfers
and played cleanup still require explicit Sync. See the spoken plan and native
planner gates; this does not authorize device deletion or public distribution.

**2026-10-13 Draco podcasts:** Zune 80/120 podcast readback now recognizes the
observed native `0x10` title-derived filename, empty redundant SeriesName and
zero MetaGenre. Podcast inventory/source maps accept Draco with strict validation;
audiobook inventory/lookup/send now also accept Draco after the isolated two-part
audio/art roundtrip. Podcast state now admits Draco Classic v2 after a 15-record
read-only identity-matched snapshot; Keel v2/Pavo v5 regression captures pass.
Cleanup still requires complete live firmware state and exact identities.
The Draco completed-playback/deletion test remains separate from this baseline. See libzune
`docs/SPOKEN_MEDIA_RESEARCH.md` for the failure evidence and retest boundaries.

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

## Submodule Discipline — READ THIS BEFORE TOUCHING libzune

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
└── libzune/                submodule — branch master
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
- See `libzune/AGENTS.md` for full conventions and `libzune/docs/TOC.md`
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
`badger-orson/zuuned`. Move public-facing links to the new account; do not alter
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

## 2026-10-05: Experimental music visualizer

Read `docs/MUSIC_VISUALIZER.md` before extending `VisualizerService`.
`ZUUNED_VISUALIZER` defaults OFF; current development builds opt in with projectM
4.1.7 and libpulse. Preserve process-specific mpv stream identity and exact
per-stream monitoring; never capture desktop audio or microphones as fallback.
OpenGL creation must wait for a usable drawable, and default draw/read buffers
must be restored explicitly. Escape preserves music and the library window.
The isolated native gate is `tests/visualizer`; existing playback regression is
`node tests/customize/run-backend.mjs --player`. Preset/product polish and
AppImage packaging remain follow-up gates, not completed release claims.

**Live mouth correction, 2026-10-05:** Zuuned Viz now uses `PortalState.h` and
`PortalRenderer.h`, independently of the unchanged Winamp/projectM preset.
The mouth is the current PCM-derived contour. Store immutable previous contours
for trails, expand/fade them, and mask them against the live mouth before drawing
its edge. Never let retained pixels determine the opening. The later approved
base-shape scheduler is described below; palette changes stay audio-driven.
Colors follow a slow separate envelope.
The response and real-GL mouth tests in `tests/visualizer` verify the actual
contour and clear opening; generic pixel movement is insufficient. User visual
acceptance and packaging remain separate. See `docs/MUSIC_VISUALIZER.md`.

**Bass/color/smoke, 2026-10-06:** Zuuned Viz uses continuous low-pass bass RMS
and a separate transient envelope so normalization cannot flatten low-end
response. Keep the slow, rate-limited audio-steered palette. Smoke surrounds
saved contours outside the live-mouth mask; it never reshapes the mouth.
Gates: 13 state, 8 GL, 14 host checks; visual acceptance remains separate.

**Sustained bands/random shapes, 2026-10-06:** the user now explicitly wants
smoothly varying base shapes as well as the live audio deformation. Preserve
`PortalShapeMorph`'s minimum hold, 3.2-second eased transitions, no immediate
family repeats and silence/pause freeze. Spectral sustain determines per-point
line width; saved contours retain their widths. Space broad bands apart and
keep the opening masked even under the thick live rim. Note duration remains
an approximation from mixed audio. Gates: 21 state, 10 GL, 14 host checks.

**Tiny mouth/travel, 2026-10-06:** keep audio-space radii separate from the 0.1
opening projection in `PortalRenderer`, and apply that projection to the mask
as well as the ribbons. Centers are viewport fractions; never amplify them by
depth scale. Historical centers ease into the path with age while contour
snapshots remain immutable. Pause freezes travel. Preserve visible foreground
walls despite the tiny opening. Gates: 25 state, 30 GL, 14 host checks; square,
ultrawide and portrait readbacks assert a nonempty clear mouth. User visual
acceptance and release packaging remain separate.

**Private checkpoint, 2026-10-08 (user decision):** the current application
snapshot, including the experimental visualizer, is for private Gitea only.
Do not publish this checkpoint or its visualizer to GitHub, export it as public
source, or include it in public releases without a new user decision. Preserve
the corresponding libzune checkpoint when restoring the application. A local
checkpoint is not a release gate or permission to push; the existing per-push
approval rule still applies.


**2026-10-09 podcast app transfer:** typed podcast queue and worker routing now
exist. Podcast show covers use DragGhost to open episode selection on device
drop; episode selections drag into the existing device queue. Feed-only selection
originally downloaded before live admission; the quiet queue correction below
moves that work into sync. Persist `deviceSourceUrl` across GUID-preserving
feed refreshes, and never use music canonicalization/identities for podcasts.
Read-only exact feed/source lookup excludes already-present episodes before
queue insertion and repeats before send. Keep the unknown-write recovery marker;
podcast auto-purge is not supported. Spoken worker jobs dispatch one at a time
because the HTTP nested event loop otherwise reenters subsequent database jobs.
Keel app-object hardware gate uploaded Moonbound episode 13, accepted show art,
finalized, and excluded the same episode on a later admission. The user confirmed
podcast playback and visible artwork on Classic; firmware resume and HD podcast
acceptance remain pending. Audiobook integration and limits are recorded below.


**2026-10-09 audiobook GUI candidate:** libzune exposes experimental
`zune_find_audiobook_part` / `zune_smuggle_audiobook_part` for Keel/Pavo.
Small two-part MP3 gates proved schema 0x12, ordered metadata and exact audio/JPEG
readback on both; the user accepted the Classic test. The HD omits native book
parts from MP3-filtered inventories, so lookup uses wildcard inventory then
schema filtering. Never infer absence from the MP3 listing. The app hashes the
reviewed whole-book source set, keeps book/part identities separate from music,
rechecks device identity before sending, copies and verifies immutable source input before audio
preparation, and reports books separately. Books use one + or cover/row drag for the whole book, with no part picker.
Chaptered M4B sources now map to ordered named device parts; intros/gaps/outros
are included. Source copies are verified once per source and reused across ranges;
large source containers are supported, prepared parts remain capped at 512 MiB.
Whole-book capacity admission is atomic. Local chapter playback remains.
Unknown writes keep manual-review recovery, never speculative purge/retry.
Firmware resume and HD visual/playback acceptance remain separate gates.


**2026-10-09 whole-book correction:** the user rejected partial book selection
and requires chaptered M4B. `queueAudiobooks` accepts book or part IDs but always
expands whole parent books. Podcast selection stays unchanged. Native range
encoding uses local-only FFmpeg decoding, PCM sample trimming, LAME delay/padding
metadata and cancellation cleanup. Never send an entire chaptered source once
per chapter or infer duplicate identity from its shared filepath. Range fields
persist in queue/recovery. Source immutability covers the entire sync using a
shared temporary copy; release it after its final range. See the latest section
of the spoken plan and `tests/audio-ranges/` for verification and hardware limits.


**2026-10-09 quiet spoken queue correction:** adding either spoken type must use
cached library metadata synchronously, through the existing queue. No file stat,
probe, full-file hash, download or live device query belongs on the add path.
No success/preparation toast or library-page busy banner for normal admission.
Connection/capacity and queued-identity checks still apply. Books remain whole-book
operations; podcast episode selection remains explicit. Drafts persist with
`needsPreparation`, including feed-only podcasts without a local path. Sync alone
requests token-correlated quiet preparation from SpokenMediaService, resolves
book hashes/episode downloads and verifies the queued manifest, then encodes and
sends with live device dedup. Cancellation/disconnect must drain the correlated
preparation before another run. Never send a provisional book key to libzune.
Use `foregroundBusy` for library task feedback; sync work belongs in the existing
queue/device progress. Keep stable bookId grouping across content-key resolution.

**2026-10-10 spoken onboarding:** optional Podcasts and Audiobooks steps follow
Photos. `PodcastPreferences` is shared with Settings → Library; keep setup edits
staged until completion and commit a focused custom-count field before saving.
Automation/cleanup remain opt-in. Audiobook import reuses the reviewed manual
folder importer; only successful import changes Skip to Next. No watch-folder
promise or implicit book sync. See the spoken plan and organization QML gates.

**2026-10-11 audiobook Libraries builder:** `BookTrayState` and `BookLibraryTray`
replace the modal spoken collection editor. The user-facing label is Libraries;
existing native collection IDs/schema remain unchanged. Local book drags into
this organizer are explicitly offline using `zuuned-local-books`; device keys
remain connection-gated. Cover/detail + feeds the draft while building. Preserve
whole-book identity, ordered deduplication, revision-checked saves, pending-write
locks and dirty replacement/cancel confirmation. Playlist/photo/book drafts stay
independent in the shared sidebar location. `RoundedArtwork` clips rectangular
spoken covers using Theme.radiusMd; fitted portraits retain their full image.
See the spoken plan and `tests/spoken-organization/` for gesture/regression gates.
