# Podcasts, audiobooks and Zune HD artist backgrounds

Private development plan, 2026-10-08.

**Scope update:** podcasts and audiobooks are the active work. HD artist
backgrounds are deferred until both are working, per the user's latest decision.
Dedicated `feature/podcasts-audiobooks` branches were created in libzune first
and then Zuuned, preserving the existing local research/probe changes.
The user requested all three workstreams for
Zuuned and libzune, with local testing and private Gitea development before wider
distribution. This is an implementation plan, not a supported-feature claim.

## Audiobook Libraries island and cover corners, 2026-10-11

The user-facing name is now **Libraries**, replacing Sublibraries. Existing
`book-collection` identities, ordered memberships and database rows are preserved;
this is a UI/workflow change, not a migration.

New/Edit Library opens `BookLibraryTray` beside the library navigation rail,
using the same floating-card placement as the playlist/photo builders. The modal
collection editor has been removed. `BookTrayState` retains the staged title,
ordered unique book IDs and original revision across page navigation. Music,
photo and book drafts remain independent; navigation or an explicit addition
selects the visible island without discarding the others.

- Create from Audiobooks → Libraries → + library, or the books add menu. The
  gallery switches to Books for browsing. Right-click a book → Add to Library
  offers a new library, existing libraries or the current draft.
- During composition, book cover/detail + actions add to the draft. Drag book
  covers or whole-book rows into the island. Organization works offline through
  the separate `zuuned-local-books` key. Connected drags also retain the existing
  whole-book device key; device drops still check connection and typed admission.
- Reorder by dragging a member's handle or using its arrows. Remove only affects
  the draft. Only Save calls native revision-checked persistence. Missing/invalid
  book IDs and duplicate additions are refused; the 5,000-book limit is retained.
- Dirty cancellation/replacement requires confirmation. Pending writes lock the
  draft. Unrelated completions are ignored; stale saves preserve the draft and
  show the native error. Deleting a library requires confirmation and never
  deletes books/files. Opening or browsing never saves or transfers anything.
- `RoundedArtwork` masks the actual image, matching the existing album-art
  radius. Spoken gallery, library and device covers, drag previews and detail
  sleeves use it. Portrait covers fitted within a square retain all their art,
  with the mask following the painted bounds. Circular disc labels keep their
  existing circular mask. Original cover URLs and device artwork are unchanged.

Validation uses production QML with recording services and isolated profiles:
52 checks on OpenGL include actual offline card drops, actual reading-order
handle drags, plus actions, pending/stale saves, dirty replacement, and deletion
confirmation. Playlist actions (8), photo builder (10) and device spoken UI (18)
regressions pass. Native spoken services pass 190 checks plus 23 planner checks.
These tests do not access USB or restart the user's application. Fixture previews
live under `/tmp/zuuned-book-library-art.png` and `/tmp/zuuned-rounded-book*.png`.
User acceptance in the complete desktop window remains a separate check.

## First-run spoken setup, 2026-10-10

The private onboarding flow now includes optional Podcasts and Audiobooks steps
between Photos and player appearance. Podcast preferences share the production
`PodcastPreferences.qml` controls with Settings → Library: starred-show queueing
on connection, 5/10/custom episode counts (1–5000), and independent played-episode
cleanup. New profiles retain opt-in automation/cleanup and a count of ten.
Per-show selections and Keep on Zune overrides retain their existing semantics.
Queueing still requires a connected device and explicit Sync starts transfers.

Setup edits stay in a local draft until completion, including the global Skip
setup action. Closing an unfinished wizard does not save podcast preferences.
Completion commits any active custom-count editor, then saves the count and
cleanup choice before enabling automatic queueing. Back navigation retains the
draft. Skipping untouched steps preserves saved preferences.

Audiobooks reuse `SpokenFolderImport` from Settings, including native folder
selection, single-book/collection mode, review and explicit import. Successful
imports persist immediately and change the step action from Skip to Next;
cancelled/failed imports do not. This remains a manual importer, not a watched
folder. Whole-book queueing and local source files are unchanged. Both spoken
sections now appear in the illustrated library/device walkthrough.

Validation: the isolated organization/setup QML suite passes 47 checks, covering
actual preference clicks, navigation, deferred saves, abandonment, invalid and
uncommitted custom counts, successful/failed import handoff and 800×600 bounds.
Native spoken tests pass 190 checks plus 23 planner checks; native settings tests
verify persistence and independent cleanup. No USB access or running-app restart
is part of these tests. Interactive user acceptance remains separate.

## Baseline and distribution

- App branch: `feature/podcasts-audiobooks`, from `103018f`.
- libzune branch: `feature/podcasts-audiobooks`, from `fbaa6e3`.
- Both baselines are preserved under `checkpoint/private-visualizer-2026-10-08`
  on private Gitea. Keep the visualizer intact on this private line.
- Public GitHub source/releases stay unchanged. Each push still requires fresh
  approval. Push a reviewed libzune commit before publishing its application pin.
- Working interpretation of HD now-playing art: full-screen artist backgrounds.
  User clarification between backgrounds, album covers or both remains pending.

## UI design study, 2026-10-08

The user requested separate Podcasts and Audiobooks tabs and visual proofs before
building their app screens. [Interactive study](../mockups/spoken-media/README.md)
contains a show library/detail, book library/detail, RSS preview, import review
and seven screenshots. It uses the existing fonts/palette/sidebar composition.
The proposed top-level entries sit alongside Music, Videos and Photos. Podcasts
have shows/unplayed/downloaded pivots; books have all/in-progress/not-started/
finished pivots and chapters/bookmarks/files details. Local downloads and device
queue additions remain distinct. Shared spoken controls cover resume, skips,
speed, sleep and bookmarks. At that mockup checkpoint audiobook transfer was
disabled; the October 9 candidate below enables reviewed unchaptered parts.

This is an in-memory browser design study, not shipped application functionality
or a webview architecture change. Production stays native. The user approved the first visual direction and requested custom covers and
drag-and-drop; the protocol and app foundation work remain on their current branch.

### Custom artwork and drag contract

The second visual pass adds a staged Customize artwork sheet and selected-episode
USB-queue drag gestures to the prototype. Apply/Cancel/reset and file decoding are
real browser-memory interactions, not database/device integration. Reuse the
native Sleeve contracts described in `SLEEVE_CUSTOMIZE.md` when implementing.

- Store manual podcast-show, episode and book artwork by stable identity, separate
  from feed/embedded candidates and grouping metadata. Explicit episode overrides
  win; a manual show cover wins over automatic episode/feed imagery. Without a
  manual choice, prefer episode art and then show art. Books prefer their manual
  cover, then local/embedded art. Provider failure keeps the last usable image.
- Apply prepares/validates off-thread and atomically saves a correlated draft;
  browsing never commits. Cancel/close/Escape discard. Reset releases only the
  artwork override when Apply succeeds. Feed refreshes, rescans and late image
  responses cannot replace a user's choice. Source files are not rewritten merely
  because a library cover was customized.
- Podcast rows drag an explicit selection to the Zune island with an item-count
  indicator. Recheck connection at drag start, drop and queue admission. Preserve
  exact episode identity, selection order and duplicate exclusions. A show-level
  drag opens episode selection, never an implicit entire-history download.
- Books drag as a whole ordered book once the transfer protocol is proven; do not
  mix chapters into music tracks. A book's chapter/part ordering stays explicit.
- Use dedicated spoken-media drag keys and typed queue payloads. Local cover
  editing stays offline; device drops require a connection. Preserve the button
  alternative and existing music/photo/playlist drag targets.
- Local artwork and device artwork are separate gates. Sending a manual cover to
  hardware still requires verified representative-sample placement and readback;
  the prototype does not claim that this is implemented.

## First native application increment, 2026-10-08

`SpokenMediaService` owns a serial worker for HTTP, SQLite, probing and artwork;
QML performs no network or database work. `SpokenStore` keeps shows, episodes,
books and parts under stable UUIDs, with a unique kind/parent/source identity.

Implemented locally:

- Dedicated native sidebar pages and bounded grid/list delegates. Customize is
  **right-click only**. The cover’s top corner uses the existing split **+ / arrow**
  component, per the latest user correction; the old visible Customize button
  in the browser study is superseded.
- RSS URL preview, subscribe, refresh, multi-selection and manual episode
  downloads. GUID identity is scoped to a feed; duplicate titles remain distinct.
  Refresh preserves downloaded files, played state, positions and custom covers.
- Local HTTP(S) validation, bounded redirects, no HTTPS downgrade, response limits,
  deadlines and cancellation. Episode bytes stream through QSaveFile and must
  pass the native audio probe before appearing as downloaded. Completed downloads
  survive a later batch failure. Signed URLs are stored privately, never logged.
- One-book file selection, metadata/chapter preview, explicit part reordering and
  atomic import. Importing the same canonical source set reuses its book. Files
  remain in place; a changed source requires a fresh preview.
- Feed/embedded cover candidates and separate manual show, episode, book and part
  overrides. Customize accepts native file selection or local image drop. Apply
  validates on the worker and checks the original cover revision; Cancel/Escape
  never commit. A manual show cover also overrides automatic episode imagery.
- Local playback through the existing mpv player, chapter seeks, 15/30-second
  skips, bookmarks, manual played state and persistent listening positions.
  Book playback retains the reviewed part order and resumes the most recently
  listened part. Spoken IDs never write to music listening-position rows.

**Transfer status:** the first increment disabled device buttons and drags.
The October 9 typed queue/worker integration below enables them for podcasts,
with live admission and recovery. The October 9 audiobook candidate adds the
equivalent typed book/part path after native Classic/HD protocol testing.
Never route spoken media through music identity/canonicalization. Local cover
image drops work without a device. The user confirmed Classic podcast audio and
visible artwork on October 9. Small native multipart MP3 book tests now pass on
Classic and HD. Chaptered preparation now has software gates (see whole-book
correction below); device resume and chaptered playback remain hardware gates.
Retention/removal,
OPML, podcast directory search and loose local podcast import remain follow-up
work.

Validation: `bash tests/spoken-media/native/run.sh` compiles the real native
services, parser/probe, PlayerService/mpv and these QML pages. It uses disposable
XDG directories, loopback HTTP, null audio and a disconnected DeviceService stub;
no USB operations or personal-library access. The gate checks transactional
persistence, cancellation, duplicate identities, custom-art revision guards,
actual Customize Apply/Cancel, book ordering, chapters, resume and stop behavior.
It captures native screenshots and rejects QML warnings. This joins the existing
34 parser/probe sanitizer checks in `ci/check.sh`. Software and live hardware
claims remain distinct.

## Shared playback and directory import, 2026-10-09

The user rejected the separate bottom playback strip. Spoken audio now uses the
same `QueueRibbonBar` and `NowPlayingIsland` migration as music. Both resolve
artwork from the spoken ID, show a native microphone/book icon, and replace music
shuffle/repeat/playlist actions with skips, bookmark, speed and more-options
controls. The existing seekable disc/vinyl and volume remain. More options provide
chapter/part navigation for books, played state, a 15/30/60-minute sleep timer,
and the top ribbon’s path into the island. The narrow sidebar has distinct
microphone and book icons; its now-playing icon follows the current media kind.

`PlayerService` identifies spoken queue entries independently of library IDs.
Speed is an actual mpv property, retained for spoken listening in the current
session and reset to 1× for music. Music shuffle/repeat preferences are preserved
but inactive during spoken queues. Sleep pauses without clearing position and
cancels when returning to music, stopping, or clearing the queue. It uses elapsed
wall time, including pauses; it is not a play-time or end-of-chapter timer.

Settings → Library → Audiobook folders, and the Audiobooks page’s folder button,
open the native folder chooser and staged review. Two explicit modes avoid
silently guessing a library layout:

- **One book:** recurse through the selected folder, including nested disc folders.
- **Book collection:** each immediate subfolder is one book; nested discs stay
  inside it. Audio directly under the selected root forms an additional book.
  An author/book hierarchy should be imported at the author level, not as one
  large author book. The review shows source directories before any write.

Traversal skips symlinks, ignores non-audio files, checks readable subdirectories,
and caps work at 100,000 entries / 5,000 audio files. Files are naturally ordered
(part 2 before part 10); review lets users change titles/author/narrator, reorder
parts and exclude books. A missing album tag uses the book folder’s name. Every
selected book commits in one transaction; stale source files or invalid later
books roll back the entire batch. Identical source sets reuse existing books.
Overlapping changed sets fail explicitly rather than cloning existing parts.
Appending/replacing parts of an existing book remains a separate edit workflow.

This is a manual folder importer, not automatic watch-folder synchronization.
New book folders can be imported again without duplicating unchanged books. No
source files, existing music rows or device contents are modified.

The native gate now exercises the actual shared ribbon and island, speed in mpv,
sleep pause/reset, music-mode restoration, folder grouping/order, native batch
Apply, repeat import and rollback. It captures the island and folder review in
its disposable profile. These remain software tests, not device transfer proof.

## Ownership

| Responsibility | Owner |
| --- | --- |
| MTP property encoding, media classification, object relationships, device inventory and readback | libzune, portable C |
| Podcast/audiobook device state and any proven resume/played properties | libzune |
| HD catalog request transport and bounded protocol parsing | libzune |
| RSS subscriptions, episode downloads, file imports, artwork selection/cache | Zuuned, native C++ worker services |
| Library persistence, local chapters/bookmarks, retention choices and screens | Zuuned |
| Serial USB ownership, cancellation, progress and disconnect recovery | DeviceService/DeviceWorker calling libzune |

No feed parsing or provider access in QML. Do not add an external runtime server
or Python service as a requirement for artist backgrounds. A native app service
can supply cached images to libzune through callbacks/events. Network fetching
must not block the UI or run concurrent MTP transactions on the device handle.

## Phase 0: protocol evidence and repeatable probes

**Capture inventory and first Classic descriptor probe complete.**
`libzune/tools/zunetool.c` now supports
`media-capabilities <format-code>` through a bounded read-only descriptor probe.
See [protocol research](../libzune/docs/SPOKEN_MEDIA_RESEARCH.md) for exact use,
reference provenance and limits. The probe performs no media writes.
Ten existing captures yielded 1,427 object-property upload datasets; none
proved a podcast or audiobook creation sequence. Existing descriptors and the
live Keel probe corroborate the podcast property types. The first probe’s MTPZ
handshake timed out. A physical reconnect cleared it; subsequent authenticated
podcast transfer and exact-byte readback passed, as recorded below.

Collect supported properties and descriptor types on Classic and HD. Preserve
format/operation failures as failures, not empty successful inventories. Capture
small known-good Windows podcast/audiobook transfers and HD image requests where
our existing captures do not establish the sequence. Evidence must identify
device model/firmware and distinguish advertised support from working behavior.

Gate: descriptors and transfer/readback fixtures sufficient to implement each
media kind. A passing podcast gate does not establish audiobook or art support.

Library integration instructions and the current property evidence are in
[libzune spoken-media usage](../libzune/docs/SPOKEN_MEDIA.md) and its
[metadata catalog](../libzune/docs/ZUNE_VENDOR_PROPERTIES.md).

## Phase 1: podcasts in libzune

**Implemented candidate:** experimental complete series inventory, safe
create/reuse and MP3 episode upload with typed properties, bounded UTF-16 array
text, live identity checks and readback/partial-failure handling. 50 sanitizer
checks pass, including read-only queue admission lookup. The Keel upload/reuse/byte-extraction/cleanup gate passes after
physical reconnect. A real Moonbound trailer subsequently passed retained upload,
repeat reuse, byte-for-byte extraction and CleanDataStore; the user confirmed it
appears under podcasts on the Classic. The initial trailer had no image. The October 9 app transfer below now sends
representative-sample show artwork; the device accepted those bytes and the user
confirmed audio and visible images. Other models, episode inventory/state,
firmware resume, artwork byte readback, references and video podcasts remain. Podcast
upload now maps the source read-only instead of allocating a full-file buffer;
the existing PTP chunk sender retains its abort and ZLP behavior. App downloads
and prepared podcast output are limited to 512 MiB. Transfer progress currently
reports start/end only, like the existing music C API.

Build typed show/episode inventory and C APIs for create, transfer, artwork,
references, extract and delete. Include audio and video episode classification,
but verify their codecs separately. Implement proper bounded UTF-16 array
encoding for URL/description fields where the device descriptor requires it.

Preserve show identity and per-episode identity. A repeated title is not a
duplicate key; use stable episode GUID/feed identity in the app and exact
device handles/readback for recovery. Inventory failure never permits blind
creation. A failed abstract-object commit must report or clean its own orphan,
never delete unrelated existing content. Read played/resume state with explicit
known/unknown availability rather than converting missing data into zero.

Gate: native fixtures for encoding, malformed readback, retries, cancellation,
duplicate admission and partial failure; a small physical show/episode roundtrip,
correct device category/art, repeat sync, playback and persistence after eject.

## Phase 2: podcasts in Zuuned

**Implemented foundation:** `src/spoken/PodcastFeed` parses bounded RSS 2.0,
iTunes extensions, relative/xml:base enclosures, publication dates and artwork.
Identity is feed-scoped opaque GUID with complete enclosure-URL fallback.
Conflicting duplicate identities fail without publishing a partial feed;
same-title episodes stay separate. The first native UI and storage increment
is now implemented; its boundaries are recorded below.

Import a feed URL and local podcast files, browse shows/episodes, download
selected episodes and play them locally with persistent resume/played state.
Start with manual downloads; add explicit retention and subscription refresh
choices without silently deleting manually imported files or device content.
Directory search/OPML can follow the working URL-to-device path.

Store feed-scoped GUID identity and an enclosure fallback, preserving manual
choices when titles, URLs or feed metadata change. Handle redirects, RSS XML
namespaces, malformed feeds, interrupted downloads and filename collisions.
Use independent spoken-media identities rather than relabeling music rows.
The first increment creates `spoken/spoken.db` schema 1 beside the existing
library, with WAL, transactions and a future-schema refusal. It does not migrate
or alter the music database; future spoken schema upgrades require backups. Use existing queue rules and theme components.

Gate: isolated feed fixtures, import/refresh without duplicates, offline local
playback, restart persistence, bounded bulk views and the full sync workflow.

## Phase 3: audiobooks

**Implemented foundation:** `src/spoken/AudiobookProbe` inspects local audio
through native FFmpeg, retaining book/author/narrator tags, disc/part numbers and
chapter source order/times. Custom local IO and a demuxer allowlist prevent
playlist/container references from opening other files or network resources.
Cancellation, probe timeout and source-stat guards reject incomplete results.
The parser gate proves file inspection. The native service gate additionally
proves local mpv resume and reviewed multipart queues with generated audio.
Neither establishes DRM or device audiobook support.

Begin with local unprotected audio: multipart MP3 books and chaptered M4B.
Separate desktop format support from what each Zune can actually play. Preserve
book/author/narrator identity, chapter/part order and local listening position.
Do not flatten chapters during preparation without preserving their mapping.

First prove device book/title/track creation and resume behavior through
libzune. Existing ZMDB type names are not an upload recipe. Do not use playlist
format 0xBA05 as an audiobook format based on the contradictory historical note.
Advertised Audible support does not establish generic audiobook or DRM support.
Record any codec, chapter or size limits discovered before exposing controls.

Gate: known-good creation capture or equivalent hardware proof, a multipart
book and chaptered input, correct ordering/category, reconnect/reindex, resume
where supported, repeat-sync identity and truthful failure reporting.

## Deferred follow-up: Zune HD artist backgrounds

Treat artist catalog imagery independently of album representative samples.
Investigate artist identity properties, the device's catalog HTTP requests and
its existing PPP-over-USB transport. Start with one cached local JPEG and one
known artist before adding provider lookup. Preserve local/custom art choices.

libzune owns device transport/state; Zuuned resolves identities and supplies
prepared cached images. Schedule tunnel polling through the same serialized
device owner as MTP transfers. Bound packets/requests, cancel on disconnect and
avoid changing existing artist IDs until the identity mapping is proven.

Gate: actual HD requests captured, custom image displayed on its physical
now-playing screen, persistence/reconnect verified, and music sync still works.
Classic devices must not be advertised as supporting this HD presentation.

## Phase 5: regression and rollout

Run existing music, video, photo, playlist, identity/recovery and visualizer
gates. Use isolated profiles for migrations and record hardware results by
device family. Review private/public differences and dependency notices before
any public export. Package only after local acceptance; publication is separate.

## Evidence to date

- October 9 follow-up: 82 native checks pass, including actual island bookmark,
  folder part-order review, native import, ribbon migration, short-window fit,
  mpv speed and sleep expiry. All 40 regression suites pass in
  `/tmp/zuuned-checks.rn3PLWsz`; the final focused native gate and 34-check
  parser/probe sanitizer gate were rerun after the final safeguards. Existing
  playback queue integration also passes. Native evidence uses generated media,
  null audio and no USB; transfer was disabled at that UI checkpoint.

- First native app increment: 53 checks pass, including actual Customize
  Apply/Cancel, local playback, multipart resume/stop, persisted state and no QML
  warnings. Parser/probe sanitizer gate remains 34/34. The existing playback
  queue integration gate (`node tests/customize/run-backend.mjs --player`) passes.
- All 40 `ci/check.sh` suites passed in this dirty private development checkout;
  evidence: `/tmp/zuuned-checks.pJzQupjB`. Subsequent store-guard and native UI
  checks were rerun in the focused native gate. These are software results,
  not an AppImage or hardware acceptance claim.

- New library gates: 31 capability and 41 podcast checks under ASan/UBSan with
  leak detection. Regression: 10 artist reuse and 26 video checks pass.
- Native app foundation: 34 sanitizer checks cover RSS parsing, duplicate
  identities, malformed/oversized input, URI handling and native audiobook
  inspection. Generated media proves two M4B chapter boundaries and MP3
  disc/part tags. The fixtures do not access personal libraries or the network.
- Library/CLI and native application build verification is recorded in
  `/tmp/zuuned-podcasts-build.log` and `/tmp/zuuned-spoken-app-build.log`.
- Live read-only series descriptors returned successfully on Keel. An initial
  authentication timeout cleared after physical reconnect. The isolated podcast
  gate then passed series/MP3 creation, repeat reuse, byte-identical extraction
  and removal of its three objects with original wildcard inventory restored.
  See [sanitized hardware evidence](../libzune/tests/fixtures/spoken-media/keel-podcast-roundtrip.txt).
  No audiobook/HD art writes occurred. Device playback, presentation after eject,
  resume, reconnect persistence and other models remain separate gates.
- The first native UI increment adds Podcasts and Audiobooks to the sidebar.
  Existing music schema and device sync routes remain unchanged. New spoken
  DeviceWorker/SyncEngine routes still need implementation. Neither feature is
  being presented as finished.

Repeat the host gates with `bash tests/spoken-media/run.sh`. The native
`spokenmediatool` target inspects a local feed or book without USB, network or
database writes:

```sh
cmake --build build --target spokenmediatool
./build/spokenmediatool rss /path/to/feed.xml https://example.org/podcast/feed.xml
./build/spokenmediatool book /path/to/book.m4b
```

The JSON includes source paths/URLs and is for local review. The tool is not
bundled in the app distribution. See [test contract](../tests/spoken-media/README.md).


## October 9: app podcast transfer and drag integration

Podcasts now enter the regular SyncEngine queue with a dedicated `podcast` type.
They never use music canonicalization, artist/album forging or MusicIdentity.
The show + adds downloaded episodes. The arrow and a show-cover drop open the
existing QuickPicker for explicit episode selection. Individual/multi-selected
rows use DragGhost and the normal device-panel drop target. Selecting feed-only
episodes downloads them before queue admission. All boundaries recheck connection;
a show drop never downloads an entire feed implicitly. Audiobook device drags
were disabled at this podcast checkpoint; the audiobook candidate below enables
explicit book/part selection through its own typed transfer path.

The worker queries live feed/source identities before inserting rows. Existing
episodes are reported as already on the Zune and excluded. Same-title episodes
are distinct; GUIDs are scoped to feeds. The selected episode's first device
source URL is persisted separately from the current enclosure URL, so a feed
refresh cannot silently change its device identity. A feed reusing one URL for
different GUIDs is refused. Queue snapshots persist this identity, cover path and
codec independently of the music library. Reconnect/stale admission callbacks
cannot publish to a different connection. At send time the worker repeats the
live lookup. Unknown interrupted podcast writes retain a manual-review marker;
there is no title-based or speculative device deletion.

Non-MP3 downloads use the existing native audio preparation path; original MP3
bytes are preserved. After verified upload, representative-sample show artwork
is attempted. Artwork refusal is reported separately from audio success. Normal
completion counts podcasts separately, clears successful rows, finalizes and
releases the device. Covers accepted by USB are not yet a visual-artwork gate.

A native regression exposed nested-event-loop reentrancy: a pending queue job
could run inside the download's network wait. SpokenMediaService now dispatches
one job at a time until its completion returns to the UI thread. The fixture
checks feed-only download followed by admission, URL changes, custom covers,
disconnect during preparation and ID validation.

Hardware evidence (Zune 30 / Keel, 2026-10-09): the separate harness links the
actual app objects and uses SpokenMediaService → SyncEngine → DeviceWorker.
It subscribed to `https://rss.buzzsprout.com/2534968.rss`, downloaded
`guid:Buzzsprout-19376357` ("13 Freedom Tastes Good"), queued one podcast and
uploaded item `268437240` under the existing show `251660022`. The device
accepted 214,600 bytes of representative-sample JPEG for the show. The app
reported one podcast, zero music tracks, swept the queue and finalized/released.
A new-session repeat excluded the episode before queue insertion with no upload
or mutation. Existing media was not purged. Raw authentication-bearing logs are
private in `/tmp/zuuned-spoken-sync-backend-AwDJkf` and
`/tmp/zuuned-spoken-sync-backend-B5cZIW`; never publish those raw directories.
The first attempt was blocked by the old app retaining USB during shutdown;
no episode was queued in that attempt.

User acceptance, 2026-10-09: the user confirmed podcasts transfer with audio
and images working perfectly on the tested Classic. This closes the playback
and visible artwork gates for that device, without establishing resume behavior.
Remaining hardware gates: verify firmware resume and repeat on HD/other models.
Audiobook upload was a separate protocol task at this checkpoint; see the
subsequent candidate below. No public push or release was made.


Latest focused software gates: 50 C podcast checks under ASan/UBSan,
94 native spoken service/QML checks (including actual show-cover, detail-cover,
selected-episode and offline mouse gestures), and 18 native queue/recovery checks.
The gesture harness walks the visual delegate tree and uses small initial mouse
moves across Qt's drag threshold before moving to the device drop target.
The current native app was rebuilt and opened on Podcasts with no QML load or
binding errors. Native tests use null audio and isolated profiles.


Full regression: all 41 suites in `bash ci/check.sh` passed, with evidence in
`/tmp/zuuned-checks.HCOizz8B` and driver log
`/tmp/zuuned-podcast-sync-ci.log`. After the final cover-ghost sizing adjustment,
the application rebuilt and all 94 native spoken checks passed again in
`/tmp/zuuned-spoken-podcast-native.log`. The final app was reopened on Podcasts;
startup evidence is `/tmp/zuuned-podcast-sync-final-launch.log`. No commit, push
or public release was performed for this increment.


## October 9: initial native audiobook GUI transfer candidate

Historical checkpoint: the whole-book correction below supersedes the initial
part picker and chaptered-source refusal.

The independent `zunetool audiobook-test` first proved two generated 45-second
MP3 parts and a JPEG on Classic / Keel and HD / Pavo. Both returned native
schema 0x12, exact book/author/part/order/duration fields and exact audio/JPEG
readback. Both normalize MetaGenre 18 to zero; accept that only alongside the
native book schema on these tested families. The user accepted the Classic test.
HD screen presentation/playback and firmware resume remain separate checks.
See libzune's [usage guide](../libzune/docs/SPOKEN_MEDIA.md) and
[protocol evidence](../libzune/docs/SPOKEN_MEDIA_RESEARCH.md).

Zuuned now admits `audiobook` rows through its existing device queue. Book covers
use the existing split + / arrow: + queues all reviewed parts, the arrow or a
book-cover drop opens explicit part selection. Selected part rows can drag onto
the Zune island. Connection is required and rechecked after asynchronous work.
Chapter display rows never masquerade as separately transferable source files.

The native spoken worker hashes the complete reviewed book source set together
with book/author identity, retaining explicit ordered part numbers. This identity
is separate from music dedup. The sync preparer copies each source privately,
verifies its queued SHA-256, then passes through MP3 or converts unchaptered audio
to MP3. Neither mutable input nor changed imported files silently reach USB.
Source and prepared part limits are 512 MiB for this first candidate. Chaptered
input returns a clear refusal until chapter-preserving preparation is available;
local chapter navigation/playback remains unchanged.

libzune owns content-keyed OverDrive folders and native part writes. Live lookup
runs before queue insertion and again before send; matching existing parts are
excluded instead of added as failed rows. Ambiguous inventory or same-name books
in a different identity folder fail explicitly. No music artist, album or
reference objects are created. Covers attach to each part. Queue snapshots,
completion counts and interrupted-write recovery retain the audiobook type.
Unknown writes require review and never trigger speculative deletion/retry.

The actual app-object HD gate caught a firmware enumeration trap: MP3-filtered
GetObjectHandles omits native audiobook tracks, including within their folder.
Wildcard enumeration includes them. Lookup now inventories all handles then
filters schema 0x12. The first attempt retained exact part ID 301991998 when
post-write lookup failed, stopped before part two, and recorded recovery. After
the fix, a fresh admission reused that first part and sent missing part 301991999,
reported one book part and zero music tracks, swept the queue and finalized.
A subsequent connection excluded both existing parts with no upload or mutation.
The generated test book is "ZUUNED GUI Book Test", folder 83887510.

Software evidence: 101 native spoken service/QML checks, including actual book
cover/row mouse gestures; 26 queue/recovery checks; 27 C audiobook boundary checks
under ASan/UBSan, including the wildcard-inventory regression. All 42 full suites
passed in `/tmp/zuuned-checks.DtlO5yi1`. The final wildcard assertion was added
and its C gate passed afterwards. Private app hardware logs are under
`/tmp/zuuned-spoken-sync-backend-pjw4Vq` and
`/tmp/zuuned-spoken-sync-backend-puENBB`; never publish raw authentication logs.
The sanitized libzune fixtures retain only explicit result summaries.

Remaining: firmware resume after leaving a book/power cycling, HD user acceptance,
long books, chaptered M4B splitting/mapping and other model families. Local
listening position is not a claim of device resume synchronization. No public
push or release was made; both repositories remain on the private feature branch.


Follow-up readback verified both app-test audio parts byte-for-byte. Part two's
app-sent JPEG also matched the prepared cover exactly. The failed first attempt
had stopped before artwork; after verifying its exact known ID, identity and
audio, the local test tool attached its missing cover and verified exact JPEG
readback. This repair touched only that newly created test object. Both test
parts remain available for device-screen/playback checks.


GUI handoff: the rebuilt `build/zuuned --page audiobooks` is running in the normal
profile and its mapped Zuuned window was verified. DeviceService connected to the
HD. Startup had no QML load/binding errors; the host portal registration warning
and existing device thumbnail failures are separate from the audiobook gate.
Launch evidence: `/tmp/zuuned-audiobook-gui-launch.log`. No other application or
terminal was closed for this handoff.


## October 9: whole-book actions and chaptered M4B preparation

The user rejected the audiobook part picker and has chaptered M4B books. Books
now show a single +. Cover and source-row drops add the whole book. There are no
book part checkboxes or picker; chapter rows retain local playback navigation.
Podcast episode selection remains unchanged. The service accepts either a book
ID or any of its source-part IDs and expands the entire reviewed book, including
all source files in order. Missing/changed input fails the whole preparation.

Embedded chapters become dedicated audiobook queue entries with unique source
and range identities, global part numbers and original chapter names. Cuts use
chapter starts: source zero through its first chapter, gaps through the next
start, and the final chapter through source duration. No opening or closing
samples disappear merely because chapter metadata leaves gaps. Range bounds and
source hash persist across queue restart and unknown-write recovery.

`zuuned_transcode_audio_range` decodes locally with native libav, seeks with
preroll, trims decoded PCM and encodes MP3 using LAME. It writes delay/padding
metadata for gapless-aware decoders. It rejects invalid ranges, external media
references and non-regular inputs; supports cancellation and removes failed
outputs. Existing whole-file music encoding is unchanged. This is app media
preparation, not a new device wire protocol. libzune continues to send the same
native ordered audiobook parts and artwork.

A source file may be larger than 512 MiB. The sync engine privately copies and
SHA-256 verifies it once, reuses that immutable source across its chapters, and
removes the copy after the last needed range. Copying checks temporary space and
cancellation. Each prepared device part remains limited to 512 MiB. Capacity
admission considers every missing chapter in a book together; if they do not fit,
the book is rejected as a whole rather than admitting a prefix.

Focused checks: 111 native service/QML checks (including intro/gap/outro ranges,
whole-book gestures without a picker and unchanged podcast gestures), 35 native
queue/recovery checks, and 34 range-encoding checks under ASan/UBSan. The latter
independently decode adjacent cuts to verify sample counts, audible content and
phase, including nonzero seeks, final endpoints and cancellation cleanup.
Read-only preparation of the imported Philosopher's Stone opening [0,62042] ms
and final five seconds ending at 31282944 ms passed. The opening decoded to
62042.018 ms (one output sample of rounding), final range to exactly 5000 ms.
Original files were untouched and test output was removed. Whole-book hardware
presentation, transitions and device resume still need testing; these checks do
not certify firmware gapless behavior. HD artist backgrounds remain deferred.


Final verification: all 43 suites passed in `/tmp/zuuned-checks.UfAeNz1s`
(driver `/tmp/zuuned-whole-book-ci.log`). A final ownership review added RAII
for prepared output even when its queued completion is discarded, and cleanup
of accepted transfer temps on engine destruction. The final rebuilt queue gate
passes 35 checks, including actual abandoned snapshot cleanup and preservation
of source bytes; music sync (21) and recovery (37) also pass after that change.
Native GUI/service remains 111 passing checks and range encoding 34 sanitizer
checks. Final local GUI launch: `/tmp/zuuned-whole-book-final-gui.log`.
Only the verified Zuuned process was restarted; no terminal or other app closed.
No chaptered-book USB transfer has been claimed or performed by these tests.


## October 9: immediate, quiet queue admission

The user rejected the delay and separate "prepare queue" banner when adding books
or podcasts. The old add path re-probed/hashed whole books and downloaded selected
podcast episodes before inserting any rows. Audio conversion was already in sync,
but admission still performed enough work to feel unresponsive.

Both add paths now synchronously build small manifests from the service's cached
library rows. They do not stat source paths, probe media, hash source files, touch
USB or download. Book +/drag still expands the complete cached chapter list;
podcast episode selection remains explicit. The normal queue updates immediately,
uses existing capacity feedback and suppresses routine successful-add/preparation
messages. Repeated adds deduplicate by saved book/episode identity. Whole-book
capacity is atomic. Connection is required at service and engine boundaries.

Draft rows persist `needsPreparation`, source IDs, source stat expectations,
chapter ranges and queued metadata. A feed-only episode may have no local path;
restore must retain it. The provisional cached book key is queue-only. Stable
bookId group keys preserve expansion when the real content key replaces it.

When the user starts sync, SyncEngine marks the existing row preparing/downloading
and requests `prepareTransfer(token,row)` on the serial spoken worker. It validates
and hashes an entire book once, or downloads the chosen episode if required, then
returns `transferPrepared(token,rows,error)` without a page notification. The
engine checks token, active device serial, exact queued identities/metadata/ranges
and updated capacity before publishing prepared payloads. Failed book preparation
blocks all its queued chapters. Source copying, chapter encoding and device sends
then use the established paths. Live device reuse remains mandatory before writes;
an existing item is skipped successfully, never represented as a failed duplicate.
Historical pre-queue hardware checks above describe the older admission timing.

`foregroundBusy` excludes sync preparation. The page's task row is reserved for
manual library operations and hides automatic position saves. Progress during
sync stays in the queue/device island. Per-token cancellation is isolated from
manual library cancellation; disconnect/stale completions cannot send to another
session. No provisional key reaches a device write. Prepared source protection,
recovery ownership and cleanup remain intact.

Focused gates: 123 native service/QML checks, 50 queue/recovery checks. They cover
immediate results without downloads/hashes, adding while background work runs,
delayed source errors, persistent feed-only drafts, stable groups, duplicate
suppression, cancellation correlation and no early worker send. Both manual USB
harnesses compile/link against the new deferred-preparation connections; neither
was executed for this UX change. Logs: `/tmp/zuuned-immediate-native.log` and
`/tmp/zuuned-immediate-sync.log`.


The final queue gate also covers compatibility with pre-draft saved books:
when bookId is absent, exact source/range identity plus book/author metadata
recognizes the already-queued chapter despite the different provisional key.
Distinct chapters and editions remain distinct. The final native build is
launched with the existing profile on Audiobooks; startup evidence is
`/tmp/zuuned-immediate-queue-gui.log`. No source/media files or queued items were
cleared for this handoff, and no USB write was run by this change's tests.


Full regression for immediate queueing: all 43 suites passed in
`/tmp/zuuned-checks.T35oYaDc` (driver `/tmp/zuuned-immediate-queue-ci.log`).
The final legacy-queue compatibility addition rebuilt successfully and its
50-check queue gate passed afterward. The mapped Zuuned window and connected
DeviceService were verified; startup shows no QML load/binding errors. No commit,
push or release was performed.


## October 9: HD podcast readback and confirmed-cleanup recovery

The user's HD sync completed all 19 audiobook parts at the transfer layer.
The following podcast episode, "01 Chaotic Beginnings", uploaded but failed
metadata verification. Pavo returned native podcast schema 0x10 with an empty
redundant SeriesName and zero MetaGenre, the same normalization already accepted
on Keel. libzune now accepts this only on Keel/Pavo native podcast objects;
series link, source URL, title, format and size remain checked. Device playback,
chapter transitions and resume are separate from the completed audiobook sends.

The episode was successfully removed by libzune, but the app unconditionally
retained a recovery marker. Both podcast mutation APIs now return the explicit
ZUNE_PODCAST_FAILED_CLEANED status only after confirmed deletion of their new
object. DeviceWorker carries this outcome to SyncEngine. A correlated confirmed
cleanup clears the marker while preserving the failed queue row and its error;
unknown or residual writes still retain recovery, including failures with ID zero.
No title matching, text parsing, automatic retry or speculative deletion is used.
The recovery banner now says a transfer needs review, without claiming the app
crashed or that the failure occurred during a previous run.

The existing old-build marker is not automatically migrated: zero ID alone does
not establish cleanup. The one observed, confirmed-cleaned test episode can have
its stale local notice dismissed without issuing another device deletion.
HD playback/artwork acceptance and the user's Classic regression test remain
pending. No hardware operations were used to validate this correction.


Correction verification: the native app builds; 57 podcast C checks pass under
ASan/UBSan, including existing Classic cases and HD normalization/reuse/lookup.
The app passes 56 spoken queue/recovery, 37 general recovery and 21 music-sync
checks, all isolated from USB. API-reference and TOC generators report current.
Logs: `/tmp/zuuned-hd-podcast-build.log`,
`/tmp/zuuned-hd-podcast-fix-tests.log`, `/tmp/zuuned-hd-podcast-sync-test.log`,
`/tmp/zuuned-hd-podcast-recovery-regression.log` and
`/tmp/zuuned-hd-podcast-music-regression.log`. This is a local development build,
not a release or a new hardware pass.


## October 9: spoken media in the Zune island

The expanded device island now includes Podcasts and Audiobooks. Its collapsed
rail uses the same microphone/book symbols as local spoken navigation. Dedicated
device pages show real device contents, independent of local library membership.
Podcast groups use native series IDs; books use parent folder IDs. Identical titles
never merge groups. Book parts sort numerically, with exact object ID tie-breaking.
Cards resolve podcast art on the series and audiobook art on the part.

Inventory is loaded lazily by the serialized DeviceWorker through four public
libzune get/free APIs. Wildcard enumeration finds native schema 0x10 and 0x12 on
Keel/Pavo, including HD parts omitted by MP3-filtered enumeration. Unsupported
families or unreadable relevant metadata report errors, not empty success.
The UI distinguishes loading, empty, disconnected and error states, offers
refresh/search, and retains successfully read contents if a later refresh fails.

Management supports selected episodes/parts and complete displayed groups.
Confirmation captures exact IDs and the current device identity. DeviceService
checks ownership and category membership; DeviceWorker rereads the live selection
and compares full metadata before any deletion. Session generation, request token
and serial protect completion from disconnect/swap races. Only confirmed deleted
IDs disappear from the model. Partial failures preserve remaining rows and report
an error. Parent folders and shared series metadata are not recursively purged.
No music identities, device playback, or local spoken imports are implied by these
controls. Sync admission and spoken browsing/deletion respect each other's busy
state; media bytes and local library rows are untouched by merely browsing.

Software gates are documented in `tests/device-spoken/README.md` and included in
`ci/check.sh`. Device-screen behavior and physical deletion remain separate user
acceptance gates. This stays on the existing private feature branches.


Device-browser verification: all 46 `ci/check.sh` suites passed in
`/tmp/zuuned-checks.Yp49rCAs` (driver `/tmp/zuuned-device-spoken-ci.log`).
The final confirmation-snapshot guard passes 16 QML checks, including actual
select-all/remove/confirmation clicks and recycled-ID refusal. Final native
facade gate passes 26 checks after the last incremental build. The C inventory
gate passes 66 ASan/UBSan checks. API reference/TOC are current at 101 public
functions. Focused logs: `/tmp/zuuned-device-spoken-qml.log`,
`/tmp/zuuned-device-spoken-backend-final.log`, and
`/tmp/zuuned-device-spoken-final-build.log`.

Only the old Zuuned window was closed using its verified compositor address.
The rebuilt application is running, its mapped window was verified, and the HD
connected. A live screenshot confirms the new microphone/book device rail
entries. No device deletion was performed. Physical browsing/deletion acceptance
remains with the user. No commit, push, AppImage or public release was made.

## October 9: consistent galleries, favorites and audiobook sublibraries

Podcasts and Audiobooks now use the existing PageHeaderBand, PivotBar,
MusicBreadcrumbBar and virtualized GalleryView, aligned to Gallery.spine.
Books retain portrait covers; shows retain square covers. Cards offer hover
playback, favorites and the existing transfer controls. Device spoken pages use
the same gallery and breadcrumb vocabulary. Sheet titles honor the selected
app display font. Playback stays in the top ribbon and now-playing island.

Podcast pivots are Shows, Starred shows and Starred episodes. Show and episode
stars are independent; the global episode view includes starred episodes from
all subscribed shows. Books have Books, Sublibraries and Starred pivots. Stars
work offline, persist in spoken.db and survive feed refresh/listening updates.
Only show, episode and book records accept favorites, not chapter rows.

Sublibraries are virtual ordered collections, suitable for series or personal
reading lists. A book can belong to several. Create one from the Sublibraries
view or the add menu; right-click a book to add it to a new or existing
sublibrary. The editor stages a name, selected books and explicit reading order.
Save commits once; cancel discards the draft. Reordering uses the Reading order
view. Right-click a sublibrary or open it and choose Edit to rename, reorder or
remove membership. Deletion requires confirmation and removes only collection
metadata. Books, parts, source files, favorites and other memberships remain.

Native service contracts:
- `setFavorite(id, favorite)` updates the latest show/episode/book record.
- `audiobookCollections()` and `collectionBooks(id)` expose collections and
  enriched books in saved order. Top-level galleries sort by title; collection
  contents retain their explicit reading order.
- `saveAudiobookCollection(id, title, bookIds, expectedRevision)` creates with
  empty ID/revision 0 or edits an exact existing revision. It validates every
  book before atomic commit, rejects duplicate/invalid members and increments
  the collection revision. Empty collections are valid. Titles are trimmed,
  required, limited to 500 characters, and may repeat across distinct IDs.
  Membership is limited to 5,000 books.
- `deleteAudiobookCollection(id, expectedRevision)` checks the revision inside
  the transaction and deletes only the collection kind. Stale edits/deletes
  fail with the draft preserved; deleted IDs cannot be revived by stale saves.

These additive records stay in spoken schema 1. They never change book parentage,
source paths, on-device grouping or transfer identities. Whole-book plus/drag,
podcast episode selection and connected-device admission remain intact. No new
USB operations are introduced by stars or local organization.

Software verification uses `tests/spoken-media/native/run.sh` for real service,
SQLite, refresh, persistence, mpv and QML behavior, plus
`tests/spoken-organization/run-qml.sh` for production-page interaction with
recording service stubs. Both use isolated profiles and no USB. The organization
suite is included in `ci/check.sh`. Visual acceptance on the user's library and
physical device behavior remain separate from these software gates.

Organization verification: the native service/QML/mpv gate passes 145 checks,
including persistent independent favorites, feed refresh preservation, collection
order, restart, invalid-member rollback, stale revisions and source-preserving
deletion. Production QML organization passes 24 checks with both software and
OpenGL rendering, including real tab clicks, narrow layouts and playback versus
cover drag. Existing device spoken QML passes 16 checks. Logs:
`/tmp/zuuned-spoken-organization-native-recheck.log`,
`/tmp/zuuned-spoken-organization-qml.log` and
`/tmp/zuuned-spoken-organization-qml-opengl.log`. The app builds and the mapped
Audiobooks window was reviewed with the existing library; covers and the active
Books pivot render correctly. The OpenGL fixture also confirms the collection
editor gradient heading. These checks do not perform USB writes.

Broader verification: all 47 `ci/check.sh` suites passed, evidence in
`/tmp/zuuned-checks.v2LYkm3Y` and driver
`/tmp/zuuned-spoken-organization-ci.log`. The final tab-click correction was
subsequently covered by the 24-check software/OpenGL organization gates and an
incremental application rebuild. Only Zuuned was restarted; its updated
Audiobooks window is mapped. No commit, push or public package was produced.

## October 9: podcast retention investigation

Tester request: remove listened-to device episodes and choose how many older
ones to add. Separate per-show selection/order from hardware completion detection.
The former can use existing feed dates and stable episode identity. The latter
needs a typed libzune state API and verified physical playback semantics.

The [libzune research record](../libzune/docs/SPOKEN_MEDIA_RESEARCH.md#podcast-listening-state-investigation-2026-10-09)
now records a complete read-only HD baseline, candidate bookmark/marked-played
fields from the previously inspected Xune reference, and the controlled playback
experiment. All three baseline episodes have zero candidate bookmark/flags.
This is not proof that they were never played, nor validation of completion.
No device media or app settings were changed and no retention feature is enabled.
The app remains closed during manual probes to preserve exclusive device access.

Future policy needs explicit opt-in, per-show limits/order, protected keep choices,
exact app-managed device identities and persistent consumed history so a refresh
cannot immediately requeue an episode removed as completed. Local download
cleanup stays independent. Unknown state, failed inventory or identity mismatch
must never authorize deletion. Readback semantics and cleanup planning belong
in separate tests; no deletion is needed for the state investigation itself.

HD follow-up: after the user's playback and physical reconnection, a complete
read-only snapshot shows `03 Like Black Smoke` retaining 275,759 ms (4:35.759)
of progress, with the candidate marked-played bit still clear. Only its bookmark
bytes changed; the other two episode records stayed identical. This supports
HD progress readback, not completed-episode cleanup. Exact hashes and record
comparison are in the libzune research record. Natural end and Classic tests
remain pending; automatic removal remains disabled.

HD completion follow-up: after the user confirmed the finish/reconnect step,
`03 Like Black Smoke` changed from bookmark 275,759 ms/flags 0 to bookmark 0/
flags `0x200`. `02 The Op` shows 9,226 ms with flags 0; episode one is unchanged.
This corroborates the HD marked-played bit for this reported completion and
separates it from partial progress. Zero bookmark alone cannot identify unplayed
content. Native state integration/retention policy are still unimplemented;
Classic, replay and broader completion semantics remain separate gates. Exact
snapshot identity, hashes and unknown additional bytes are in libzune research.

Classic follow-up: an authenticated, complete Zune 30 snapshot exposes seven
podcast records. `01 Chaotic Beginnings` carries flag `0x200` alongside 40,599 ms
of saved progress; `13 Freedom Tastes Good` carries 90,771 ms with flags zero.
Other episodes have zero bookmark/flags. No before-play Classic baseline exists;
user playback/marking history needs clarification before claiming natural EOF
or replay semantics. The offline inspector was corrected to accept the actual
33-descriptor Classic layout (HD has 97); 35 checks and both real snapshots pass.
Keep bookmark and played state independent. No production state API, automatic
cleanup or device media writes were added by this investigation.

Classic clarification: the user reports playback finished while they were away
for about two hours, but cannot reconstruct the sequence. This supports the
observed marked-played state on Classic as well as HD; the Classic's retained
40.6-second bookmark remains unexplained. Native state should expose bookmark
and marked-played separately, with explicit availability. Replay behavior and
retention decisions still need their own policy and tests; cleanup stays off.

## October 9: optional played-podcast cleanup and episode selection

The investigation above is now integrated into the private application candidate.
Settings → Library → Podcast cleanup offers **Remove played episodes on sync**, off
by default. Pressing Sync runs cleanup before transfers, even with an empty queue.
The firmware's `0x200` marked-played bit is authoritative for this policy; a zero
bookmark or elapsed-time guess is not. The Classic can retain a bookmark while
marked played. Local/manual played flags alone never authorize device deletion.

The device worker reads complete authenticated listening state and live podcast
identities. Only an exact locally known feed/enclosure match with agreeing object,
series, title and duration is eligible. Unknown, incomplete, ambiguous, unsupported
or changed state cannot authorize removal. Podcasts alone participate; books and
music retain their existing transfer and deletion behavior.

Before deletion, the native spoken store atomically records device-scoped consumed
history. Failed persistence means no deletion. The worker then reads/revalidates the
whole selected plan again before deleting its first object. Token, device serial,
session generation and cancellation remain checked throughout. Confirmed partial
success updates the browser and count; unconfirmed deletions are never reported as
successful. Cleanup errors are shown, and ordinary transfer work may continue when
the device remains usable. No local download or source file is removed.

An episode's right-click **Keep on Zune** protects it from cleanup and allows a
previously consumed episode to be queued again. This is independent of starring.
Keep choices and the global cleanup setting cannot be changed through the UI during
a sync. Current store choices are revalidated before persisting played history.
Consumed history survives restart/feed refresh and suppresses re-addition on the
same Zune while cleanup is enabled, including a queue saved before an interrupted
cleanup. Other devices and protected episodes retain their own admission behavior.

The podcast cover arrow and show drag picker now offer **Newest**, **Oldest** and
**Manual** modes. Newest/oldest select a requested count from 1 to 5,000, defaulting
to three unplayed episodes. The unplayed filter is optional. Actual publication
instants determine order, including timestamp offsets; undated items sort last,
and equal dates use stable identity ties. Review/check boxes remain available and
Manual exposes all episodes. Per-show count/order/filter are remembered when a
preset selection is added to the queue. Closing the picker changes nothing.
Connection and sync-state checks still apply at admission. Downloads/preparation
remain in the normal sync queue, never in the picker.

These are deliberate queue additions, not a background subscription scheduler or
a maximum number of episodes retained on a device. Refreshing a feed does not
silently add content. Manual episode checkboxes, selected-episode drag, and the
existing downloaded-episode plus action remain available.

Software gates: `libzune/tests/run-podcast-state.sh`,
`libzune/tests/podcast-state-inspector.mjs`, `tests/podcast-cleanup-worker/run.mjs`,
`tests/spoken-sync/run-backend.mjs`, `tests/spoken-media/native/run.sh`, and
`tests/spoken-organization/run-qml.sh`. All hardware captures used in these gates
are read-only prior evidence. End-to-end cleanup through the GUI still needs user
acceptance on Classic and HD. No device media was deleted to test this candidate;
no public package or push is implied by a successful software gate.

Live read-only Classic verification of the integrated APIs: authenticated Keel
returned seven listening states, seven podcast inventory rows and seven exact
feed/source mappings. Played object `0x100006f9` agrees on title, series, parent and
1,573,235 ms duration; bookmark remains 40,599 ms. One played row, zero join
mismatches. Probe only called breach, state/inventory/source reads and sever;
no media mutation or purge. Local evidence:
`/tmp/zuuned-podcast-cleanup-readonly.log` and its temporary C probe. This proves
current Classic readback compatibility, not GUI cleanup/deletion acceptance.

Candidate verification: all 50 `ci/check.sh` suites passed
(`/tmp/zuuned-checks.LF956RxD`, driver `/tmp/zuuned-podcast-cleanup-ci.log`).
Focused results include 184 native spoken checks, 82 actual SyncEngine checks,
37 wrapped worker checks, 108 native state checks and 74 podcast protocol checks.
The subsequent summary presentation correction was rebuilt and rechecked with
82 SyncEngine checks and 28 production-QML interaction checks; it publishes the
current removal count/warning before opening the completed-sync sheet. Evidence:
`/tmp/zuuned-podcast-cleanup-engine-final.log` and
`/tmp/zuuned-podcast-cleanup-qml-final.log`. No physical deletion was performed.

## October 10: podcast status badges and visible removal actions

Local podcast episode rows show a played indicator for local played history or
confirmed firmware played state. Firmware-only state is labeled **played on
Zune** without overwriting local/manual listening choices. All remains the default
view, including played episodes; Unplayed uses known local/device status.
The existing `OnDeviceBadge` component and `images/zuuned.png` mark exact podcast
membership in the local episode list, picker and show covers (at least one episode).
No title-only match is used. Unknown readback never invents a badge or played flag.

Opening Podcasts requests one serialized native spoken inventory per connection.
Busy-device requests defer; repeated UI changes do not trigger repeated scans.
Exact membership comes from feed/enclosure source inventory; listening state is
optional and joins only agreeing live item/series/title/duration records. Source
read failures preserve podcast/book browsing and exact positive membership, but
leave missing matches unknown. A separate status warning records incomplete
readback. State-only failures preserve known membership with unknown played
status. Manual removal keeps its original category/parent/metadata validation
without depending on these optional status reads. Successful sends/reuse update positive
membership for the captured session only. Disconnect and confirmed deletion clear
or prune badges. QML reads cached status; it does no protocol work.

The cover-arrow/show-drag picker defaults to **Newest 10** for shows without a
saved preference. Count, oldest/newest order and the optional unplayed filter
remain customizable per show. Existing preferences win over the new default.
Manual mode still exposes individual episodes, including played ones. Already
present episodes remain visible with the PNG badge but are not selected for a
new transfer. This remains a reviewed queue action, not automatic replenishment.

Completed-episode cleanup now creates distinct transient `podcast-removal` rows
in the normal queue after a valid device inspection. Each episode stays visible,
including within grouped rows, with a **Completed episode** reason label.
States distinguish pending removal, removing,
confirmed removed, not removed and removal unconfirmed. Progress is correlated by
cleanup token, serial, connection generation and item ID. The final worker result
remains authoritative for removal counts and pruning the device inventory.
Removal rows cost no upload capacity, do not count as uploads and cannot become
transfer retries. They remain for outcome review until the next sync/clear, but
are excluded from both saved queues and restored deletion requests. Restart never
replays a stale removal plan. Local downloads remain untouched.

Candidate verification: all 51 `ci/check.sh` gates passed, including 187 native
spoken checks, 102 SyncEngine checks, 54 cleanup-worker checks, 49 device-facade
checks, 31 spoken-organization QML checks and 13 queue QML checks. Evidence:
`/tmp/zuuned-checks.fyZ6mpqc`, driver `/tmp/zuuned-podcast-status-ci.log`.
The Completed episode reason label was included in the queue QML gate, then
rebuilt into the application (`/tmp/zuuned-podcast-status-build-final.log`).
These are isolated software tests, not a new physical cleanup/deletion claim.
GUI cleanup acceptance on Classic and HD remains with the user.

### October 10: cleanup discoverability correction

The first live follow-up sync transferred a new episode but skipped cleanup:
the test profile had no `podcastAutoCleanup` value, so the optional setting was
false. This was configuration, not evidence of a failed device deletion. The
setting is now directly visible on both local and device podcast pages through
one shared control, remains shared with Settings, and cannot change mid-sync.
The device podcast browser also displays confirmed firmware played state;
previously that label existed only in the local episode list. Unknown firmware
state never becomes a played label. Sync logs now report disabled cleanup and
candidate/validated-removal counts without exposing episode sources.
The user's existing test profile is enabled for the requested cleanup test;
the default for other profiles remains unchanged. No sync is started merely
by enabling the option or opening a page.

Correction gates: 18 device-browser QML, 31 local organization QML, 187 native
spoken and 102 SyncEngine checks passed. The application rebuilt successfully
and reopened with the existing profile. Evidence is in
`/tmp/zuuned-podcast-visible-{device-qml,local-qml,native,sync}.log` and
`/tmp/zuuned-podcast-visible-cleanup-build.log`. No live cleanup was initiated
by this correction; the next user-started sync is the hardware acceptance test.

### October 10: preview cleanup before pressing Sync

Completed-episode removal rows now appear while reviewing the queue, based on
the current connection's cached, correlated feed/source/firmware played status.
Enabling cleanup requests the serialized spoken inventory even when the local
podcast page is not open. Device refresh/disconnect, retention changes and the
cleanup setting rebuild pending previews; completed outcome rows remain for
review. Previewing performs no deletion or played-history write. At sync start
the preview is discarded and the existing authenticated whole-plan validation
still runs before any deletion.

Removing a pending removal row or group, or clearing the queue, excludes those
local episode identities from cleanup for this connection. The same exclusion
filters the actual inspection candidates, so clearing a visible action cannot
leave a hidden deletion. Reconnecting resets these temporary exclusions; use
Keep on Zune for persistent protection. Previews stay out of saved queues and
capacity estimates, and unknown or ambiguous playback/source state cannot
create a removal preview.

Preview verification: 113 SyncEngine, 21 music sync identity and 13 queue QML
checks passed; the app rebuilt. Evidence:
`/tmp/zuuned-cleanup-preview-{sync,music,queue}.log` and
`/tmp/zuuned-cleanup-preview-build.log`. Live read-only GUI inspection on the
connected Classic showed the two retained upload entries plus **01 Chaotic
Beginnings**, labeled **Completed episode** / **Will remove on sync** before
pressing Sync. Screenshot: `/tmp/zuuned-cleanup-preview-window.png`; application
log: `/tmp/zuuned-cleanup-preview-app.log`. Queue left open; no live deletion
or sync was initiated during verification.

## October 10: spoken drill-down design parity

Local show/book details and the device spoken browser now share
`SpokenDetailHeading`: the album-detail category/title/author hierarchy,
orange-to-pink display typography, secondary counts and quiet inline actions.
Cover and identity sit side by side on wider pages and stack below the shared
detail breakpoint. Titles opt into wrapping in `GradientText`; other callers
retain their existing single-line behavior. Action flows wrap on narrow pages.
Audiobook headings count chapters separately from their underlying files.

The shared `SpokenShelf` uses the album-detail tilted sleeve and partially
exposed disc, with a hover slide and the existing vinyl/disc preference. Portrait
book covers stay uncropped. Playback and the pink + sit beside the identity,
not over the cover. Podcast cleanup remains on the Shows page and in Settings.

Episode/part lists use the album frame width and 34-pixel single-line rows,
number/title/author columns, subtle separators and aligned durations. Selection
checkboxes and playback/download controls appear on hover or active selection;
played status, device badges and existing favorites remain visible. Existing breadcrumbs and
underlined pivots remain. Bulk podcast actions appear with a selection and wrap
below the pivots instead of crowding the same row. Cover drags, split +/picker,
whole-book admission, episode selection, played/device badges and right-click
Customize retain their existing service contracts. Device removal still requires
the existing confirmation. No protocol, transfer, cleanup or playback logic was
changed for this visual correction.

Verification: 35 spoken organization checks passed with OpenGL, including long
titles at wide and narrow detail sizes; 18 device browser checks and 187 native
spoken checks passed. The application rebuilt successfully. Evidence is in
`/tmp/zuuned-spoken-shelf-{local,device,native,build}.log`. This is a UI correction;
no hardware transfer or deletion was performed for this verification.

### Feed episode numbers

The podcast row gutter displays the positive integer from RSS `itunes:episode`,
never the row position or a number guessed from the title. Missing, invalid,
zero and overflowing values remain blank. The parser resolves the iTunes
namespace rather than depending on its XML prefix. `episodeNumber` is additive
metadata in the existing JSON store; subscription/refresh saves it without
changing episode identity, order, playback state or device transfer metadata.
A refresh clears a previously supplied number if the feed removes it. Existing
subscriptions populate this field on their next feed refresh. Audiobook part
numbers and chapter markers remain separate.

Verification: 45 parser/probe checks under ASan/UBSan, 190 native service
checks and 36 QML organization checks pass. Coverage includes malformed or
missing numbers, persistence, refresh correction/removal, and stable numbering
in filtered/starred lists. Build passes. Logs: `/tmp/zuuned-episode-number-*.log`.

## October 10: starred podcast queue automation

Settings → Library → Starred podcasts adds opt-in `podcastAutoQueue` (off by
default) and a 5/10/custom `podcastAutoQueueCount` (default 10, bounded 1–5000).
A saved per-show count overrides the default. On connection, enabling the setting,
changing its count, or starring another show, native `PodcastQueuePlanner`
refreshes eligible shows and stages their newest unplayed selection. Automatic
selection always uses newest/unplayed; the show + instead honors its saved
newest/oldest/count/played selection. The arrow still supports individual picks.
Both + actions now accept feed-only episodes and refresh first, replacing the
old downloaded-file-only behavior. Audio downloads/preparation remain in Sync.

Selection checks the complete ordered feed and filters local played state, exact
live device played state, and consumed history for the connected device before
taking N. Existing on-device episodes count toward N and are omitted from uploads.
Keep on Zune protects cleanup and permits explicit re-adds; it does not cause
played episodes to be selected automatically. Kept episodes can remain beyond N.
An older unplayed episode is never deleted simply because newer episodes arrived.
Existing cleanup opt-in, preview rows, whole-plan validation and explicit Sync
remain authoritative. Queue automation does not write or delete device objects.

The coordinator uses retained feed/source identities and requires known device
membership. Unknown/ambiguous inventory cannot mean absent. A shared enclosure
identity across different episodes blocks that show's automatic selection. Feed
failures leave existing queue rows alone and do not prevent healthy feeds from
being considered. Failed inventory pauses prepared selection until recovery.
Connection generation and serial checks discard stale callbacks, including a
same-device reconnect. Sync and a queue belonging to another device defer work.
Admission waits for pending local edits so unstar/played changes made during a
feed refresh apply first. Global settings changes invalidate in-flight selection.

One pass per connection/show/count prevents library revisions or normal queue
changes from re-adding dismissed entries. Explicit Clear queue cancels pending
feed additions too. Reconnect, re-enable, a count change or an explicit + can
request a new selection. Unstarring prevents future automation; it does not
silently delete already-reviewed queue rows. A failed feed retries on another
connection or explicit +, without a network retry loop.

Verification: the application builds; 23 planner lifecycle/selection checks plus
190 existing native spoken checks pass, including loopback feed refresh and
feed-only queue drafts without audio downloads. Also passing: 38 organization
QML, 10 native settings, 113 spoken sync, 49 device facade and 13 queue QML checks.
Logs: `/tmp/zuuned-podcast-planner-native.log` and
`/tmp/zuuned-podcast-auto-{build,ui-final,settings,sync,device,queue}.log`.
No physical transfer or deletion was initiated for this verification. User
acceptance of connect-and-review behavior on Classic/HD remains a hardware gate.

## October 10: podcast filename readback correction

The Classic Nontendo #190 failure was reproduced with an isolated generated tone
using the same title. The device returns the literal title plus `.mp3`, preserving
`?` and `|`. libzune now recognizes that exact native-podcast derivation without
weakening outgoing basenames, identity or size verification. The corrected test
passed repeat reuse and byte-for-byte extraction, cleaned only its own objects,
and restored the original 1396 wildcard handles. The app rebuild includes this
fix; full real-episode playback remains for user testing. See libzune's
[hardware record](../libzune/docs/SPOKEN_MEDIA_RESEARCH.md#keel-title-derived-podcast-filename-2026-10-10).

Real GUI follow-up: all 19 queued podcasts, including the original Nontendo #190,
completed payload/metadata verification and artwork; no filename mismatches or
failed/pending podcast rows remained. The app proceeded to its 49-video phase.
The observation did not stop or restart that running sync. Device playback remains
separate. Evidence: `/tmp/zuuned-filename-fixed-app.log`.

## October 10: private Gitea checkpoint verification

`bash ci/check.sh` passed all 52 software gates for this complete working-tree
checkpoint, covering spoken media, protocol fixtures, sync/recovery, photos,
playlists, music identity/import, artwork, diagnostics and packaging rules.
Evidence: `/tmp/zuuned-checks.IyTn03cA/results.txt` and its per-gate logs.
libzune was committed/pushed first as `853bf3c`; the app pins that private commit.
The running Zuuned sync was left open throughout the commit/push operation.
This checkpoint is private Gitea work, not a public release or a claim that all
remaining hardware playback/resume gates have passed.
