# Spoken media and HD artist-art research

Private development, 2026-10-08. Experimental podcast series inventory/create
and MP3 episode transfer APIs are implemented and software-tested. An isolated
upload, reuse, byte-for-byte extraction and cleanup gate passes on Keel. Audiobook
transfer now has experimental APIs and a two-part Keel/Pavo hardware gate;
HD artist catalog transport remains deferred. On October 9 the user
confirmed podcast audio and images work on the tested Classic. Resume and
other-model acceptance remain pending. This document separates
research evidence from implemented and physically verified behavior.

Practical usage: [Spoken-media integration](SPOKEN_MEDIA.md).

## Current evidence

ZMDB research identifies show/episode types 0x0F/0x10 and book/title-track types
0x11/0x12. Naming a record type does not prove its write contract.

Our [wire findings](WIRE_CAPTURE_FINDINGS.md) document advertised Audible format
0xB904, with no audiobook transfer in the examined captures. The same historical
paragraph labels 0xBA05 an audiobook format, conflicting with our implemented
playlist constant and upload path. That label must not drive new audiobook code.

External reference inspected at XuneSyncLibrary commit
`c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4`:

- [Podcast writer](https://github.com/xune-software/XuneSyncLibrary/blob/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/ZuneMtpWriter.cpp)
  implements zero-byte series creation (0xBA0B), separately classified audio and
  video episodes and representative-sample series artwork. These are reference
  sequences, not hardware validation of our C implementation.
- [Property definitions](https://github.com/xune-software/XuneSyncLibrary/blob/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/ZuneMtpWriter.h)
  identify IsPodcast 0xDA9C, series link 0xDA9E, SourceURL 0xDD60 and Description
  0xDC48. URLs/descriptions use AUINT16, not ordinary count-prefixed MTP strings.
  Episode MetaGenre values are 64 audio and 65 video in that implementation.
  Its unterminated AUINT16 write encoding is not suitable for the tested Keel
  firmware: our physical gate below requires a counted terminal NUL.
- [HTTP request parser contract](https://github.com/xune-software/XuneSyncLibrary/blob/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/protocols/http/HTTPParser.h)
  includes artist biography, image-list and deviceBackgroundImage routes.
  Our own [vendor-opcode captures](ZUNE_VENDOR_OPS.md) independently document
  the PPP-over-USB tunnel, which current libzune has not implemented.
- An audiobook CLI exists in that snapshot, but references
  `UploadTrackWithMetadata`, whose implementation is absent from that snapshot
  (the `MediaType::Audiobook` enum does exist). Historical investigation below
  recovers the former upload path; it still requires our own hardware proof.

No external implementation code was copied for this phase. Keep reference
attribution distinct from our own captures and hardware gates.

## Our decoded captures

Examined ten existing captures in the local `Downloads/zune` collection:
`USB-capture`, `album copy`, `albumsync`, `capture-bulkreading`, `episodepull`,
`episodesync`, `newzune`, `photocopy`, `photosync`, and `tvcopytocollection`
(all `.pcapng`). A bounded split-container decoder recovered 1,427
SendObjectPropList datasets with no incomplete selected datasets. The observed
uploads were music, photos, video and their folders/abstract objects. No
podcast-series creation, podcast MetaGenre 64/65 upload or proven audiobook
creation was found. A capture named `episodesync` is not, by itself, evidence
of podcast sync: its media had TV MetaGenre 38.

The Windows property descriptor responses do corroborate the series types:

| Capture / frame | Format | Property | Type | Advertised access |
| --- | --- | --- | --- | --- |
| `newzune.pcapng` / 5023 | 0xBA0B | 0xDA9D series flag | UINT32 | get/set |
| `newzune.pcapng` / 5039 | 0xBA0B | 0xDA9C IsPodcast | UINT8 | get/set |
| `newzune.pcapng` / 5047 | 0xBA0B | 0xDD60 SourceURL | AUINT16 | get/set |
| `USB-capture.pcapng` / 4438, 4454, 4462 | 0xBA0B | same three properties | same types | get/set |

`newzune.pcapng` also describes MP3 Description (frame 1639) and SourceURL
(1663) as AUINT16, SeriesName (1815) as STR, series handle (1871) as UINT32,
and MetaGenre (1799) as UINT16. The descriptors establish types, not text
termination rules. AUINT16 uses a 32-bit unit count, while MTP STR uses a
one-byte count. Our later physical test establishes the counted terminal NUL
requirement for podcast URL/description writes. Raw captures/authentication
data have not been added to source.

## Experimental C implementation

- `zune_get_podcast_series` returns complete ID/name/author/feed identities.
  Failure is distinct from empty; partial inventories never authorize creation.
- `zune_forge_podcast_series` reuses exactly one live exact-feed match or sends
  the six series properties and commits the zero-byte abstract object.
- `zune_smuggle_podcast_episode` sends caller-prepared MP3 bytes with MetaGenre
  64, series link, source URL and description. It verifies the selected series,
  advertised property types, post-upload values and object size. Same-title
  episodes are not merged. Exact series/URL reuse is scoped to the supplied
  parent folder and additionally requires matching title/size.
- `zune_free_podcast_series` releases the owned inventory. All device operations
  remain synchronous and must share the application's serialized device owner.

Podcast content inventory and writes require the internal `mtpz_authenticated` flag set only after
a successful handshake. This does not change the handshake or the existing
lenient connection API; a non-NULL handle alone is insufficient. Unauthenticated content inventories fail
instead of presenting an empty library. Read-only capability probes remain usable.

URL/description encoding is strict bounded UTF-8 to counted UTF-16, including
surrogate pairs and one counted terminal NUL. Readback accepts terminated or
unterminated text, rejects interior/multiple NULs and does not discard a BOM.
Ordinary short MTP strings that would be truncated or altered
by the existing codec are explicitly rejected. Required output IDs expose
known newly created orphans when cleanup fails. An invalid/missing creation
handle stops further work, with no inferred cleanup ID. Reserved all-object
handles can never reach deletion. Never blindly retry an ambiguous failure.

Limits: audio MP3 only; caller owns codec preparation and immutable source-file
lifetime. The current MTP file-send helper buffers the file, so bounded-memory
streaming remains required before large-episode rollout. These APIs do not yet
provide episode inventory/state, video podcasts, chapter mapping, reference
updates, artwork orchestration or bookmark/played-state support. Exact URL
matching is not a replacement for app-side feed/GUID persistence when URLs
change. No implicit finalization occurs. Reusing a manually renamed series
requires passing its live name when subsequently creating an episode.

## Read-only hardware observation, 2026-10-08

After normally closing Zuuned to release USB, the connected 045e:0710 device
identified as Keel (Zune 30). The series-format probe returned 19 descriptor
headers and completed successfully. All six series creation properties above
were advertised with their expected types and get/set access. The sanitized
report is [keel-series-descriptors.txt](../tests/fixtures/spoken-media/keel-series-descriptors.txt).

The connection first hit an OpenSession timeout, recovered using the existing
reset path, then timed out during the MTPZ certificate request. The library
continued and descriptor reads worked despite failed authentication. Therefore
this is **advertisement evidence only**, not authenticated transfer proof. No
media was uploaded/deleted/finalized by the probe. A clean physical reconnect
and successful authentication are needed before the transfer gate.

Manual retry with Zuuned closed: `zunetool podcasts` again timed out at
OpenSession, recovered, then timed out on the certificate request (60 seconds,
synthetic response 0x02FF). After that process exited and released USB,
`usbreset 045e:0710` succeeded. A second manual session opened without the
OpenSession timeout, but the same certificate request still timed out. Both
inventory calls returned zero series after the failed handshake; this is not
authenticated evidence that the device has no podcasts. Both sessions severed
normally, and no application/test process retained USB ownership. No media
writes, deletes or finalization were attempted. A physical reconnect/device
restart is the next recovery step; repeating software resets is not evidence
of progress. Authentication/USB/session implementation files were unchanged
by the podcast work at this point.

## Keel upload/readback gate, 2026-10-08

A physical unplug/replug restored successful authentication. The explicit
`zunetool podcast-test <short-mp3>` harness then exercised a generated two-second
MP3 in a unique temporary folder. It records a pre-test handle inventory, refuses
to write after authentication failure, and cleans only newly returned handles
that were absent from that inventory. The test does not accept existing IDs.
SIGINT/SIGTERM finish the small active operation before cleanup. Failed child
cleanup leaves the test folder for review.

The final gate passes: show creation/readback, exact-feed show reuse, episode
creation/readback, exact series/URL episode reuse, full extraction with identical
bytes, removal of all three created objects, and restoration of the original
wildcard handle inventory. [Sanitized result](../tests/fixtures/spoken-media/keel-podcast-roundtrip.txt).
The app stayed closed; no existing media was renamed, overwritten or removed.

Physical findings now covered by the implementation:

- Unterminated SourceURL writes were accepted but came back with an extra ASCII
  `0` and a NUL. A counted terminal NUL on the **write** fixes the stored value.
  Merely relaxing the reader is insufficient. URL/description identity remains
  exact after decoding; interior terminators still fail.
- Keel assigns an episode filename from its title. Readback validates a safe
  `.mp3` basename, while identity uses series/URL and the returned object ID.
  Filename is not an episode identity or interrupted-transfer recovery key.
- Firmware assigns audio podcast atom schema `0x10` and retains the correct
  series handle, but reads MetaGenre as zero and redundant SeriesName as empty.
  Only Keel objects with that schema may use this interpretation; ordinary music
  schema and unknown models cannot. Series handle/URL/title and MP3 format/size
  still must match. This is protocol classification evidence, not visual
  acceptance of the device's podcast screen.
- DateAuthored comes back with `.0` instead of `Z`. Whole-second date/time
  components must still match; changed timestamps, nonzero fractions and
  timezone offsets are rejected by the comparison.
- Wildcard inventory grew from 1,383 to 1,385 for three created objects. It is
  not an exhaustive podcast inventory. Typed series/episode lookups proved
  reuse; every original wildcard-visible handle survived and the original
  set/count returned after cleanup.

No generated fixture was left by that cleanup gate. No audiobook write behavior
is established.

### Real feed and retained episode

The app's native RSS parser accepted `https://rss.buzzsprout.com/2534968.rss`
(Moonbound): 29 episodes, none skipped. The user selected this feed and explicitly
requested a tooling-only transfer. Its shortest item, `EXODUS - MoonBound - Trailer`,
was downloaded and inspected: 1,560,414 bytes, approximately 127.9 seconds,
96 kbps mono MP3 at 44.1 kHz, ID3v2.3, with embedded JPEG artwork.

`zunetool podcast-send` retained this real episode on Keel, using the original
feed URL, enclosure URL, author, title and publication date. Show and episode
repeat calls reused the same IDs; complete extracted bytes matched the original
MP3; CleanDataStore succeeded, then the session closed. Retained IDs:
folder `83887861`, series `251660022`, episode `268437239`.
The user subsequently confirmed that the podcast appears on the device, with
no image. This passes the observed category-presentation gate on this Classic.
Playback, reconnect persistence, listening state and other models remain unverified.

Artwork transfer is not implemented by this command. The feed supplies show art,
and the MP3 contains embedded art, but that alone did not populate the observed
podcast image. Representative-sample placement and device presentation still
needed a dedicated test; do not label the feed as missing artwork. The October 9
app queue gate below subsequently sent show art, and the user confirmed both
audio and visible images. The CLI itself still does not send artwork.

The developer command accepts a caller-prepared MP3 up to 16 MiB:

```sh
./zunetool podcast-send /path/to/episode.mp3 FEED_URL SHOW TITLE AUTHOR EPISODE_URL YYYYMMDDTHHMMSSZ
```

Quote arguments with spaces. This command does not fetch a feed or transcode.
It requires authentication, uses exact feed identity to find a show and its
folder, verifies repeat reuse and downloaded bytes, and retains successful
content for user playback tests. It never deletes existing content. On failure,
the C APIs clean their own failed new objects where possible; the CLI reports
remaining folder/show/episode handles for inspection, with no automatic folder
cleanup. Do not treat its lightweight input probe as general codec certification.
Raw connection logs are private and must not be published without sanitization.

## Read-only format probe

Close/eject the application connection before running the harness. Only one
owner may use the device. Build and run against a chosen format:

```sh
make zunetool
./zunetool media-capabilities 0xBA0B > podcast-series-capabilities.txt
# Separate sessions, as needed:
./zunetool media-capabilities 0x3009 > mp3-capabilities.txt
./zunetool media-capabilities 0xB218 > artist-capabilities.txt
./zunetool podcasts  # Read-only complete series inventory; lists names/authors
```

The normal connection/authentication lifecycle still runs. The probe itself
uses only GetObjectPropsSupported and GetObjectPropDesc; it does not enumerate
personal media, upload, delete, finalize or modify object properties. Output
contains property codes, type codes, advertised get/set flags and descriptor
lengths, never authentication payloads or individual object values. Existing
connection logs remain on stderr and should be reviewed before sharing.

Only descriptor common headers are interpreted. Default values, group codes
and forms are not validated, so reported access flags do not prove that writes
will work. Empty inventory is distinct from failure and is not a claim that the
format supports creation. Errors preserve the PTP response and stop further
descriptor queries. Exit status: 0 completed report, 1 query/output failure,
2 invalid command arguments (before connection). At most 256 descriptors are
queried; malformed lists/headers fail rather than inventing capabilities.

The MTP supported-property decoder now checks counts without overflow and
returns a successful empty list without relying on malloc(0).

The separate write gate is explicit and limited to a prepared MP3 at most
30 seconds / 1 MiB: `./zunetool podcast-test /path/to/generated-short.mp3`.
It is never part of automatic capability probing or software CI.

## Verification

```sh
bash tests/run-media-capabilities.sh
bash tests/run-podcasts.sh
bash tests/run-artist-reuse.sh
bash tests/run-video-named.sh
```

31 probe/decoder, 50 podcast, 10 artist reuse and 26 video checks pass with
ASan/UBSan and leak detection. The podcast suite runs real MTP packing and C
operations over a fake PTP transport. The separate Keel gate above provides
specific physical evidence, not a blanket device-support claim.
Coverage includes Unicode/long URLs, malformed inventory, repeated/ambiguous
identities, descriptor refusal, invalid timestamps, cancellation, partial
commits, readback mismatch and cleanup failure. CLI/native app build gates pass.
See the parent application's `docs/SPOKEN_MEDIA_AND_DEVICE_ART.md` for the
remaining implementation phases and local RSS/audiobook inspection gates.


## October 9 app queue gate

`zune_find_podcast_episode` is a read-only queue-admission lookup. It returns the
actual series parent plus the exact feed/source episode, rejects ambiguous or
unreadable inventory, and requires successful MTPZ authentication. It verifies
podcast classification using the existing model-specific readback rules. It does
not compare audio bytes or overwrite metadata. The caller freezes the original
device source URL for each feed/GUID identity across enclosure URL refreshes.
The C fixture now passes 50 sanitizer checks, including this lookup's failure,
classification, parent and duplicate contracts.

Podcast sends map the read-only prepared file rather than allocating an entire
second audio buffer. PTP bulk chunking, payload-based ZLP decisions and abort
checks remain unchanged. The caller must keep the source unchanged through the
synchronous call. There is no per-chunk application progress callback yet.

The app's real service/queue/device-worker harness uploaded Moonbound episode 13,
"Freedom Tastes Good", from `https://rss.buzzsprout.com/2534968.rss` to Keel.
Existing series `251660022` was reused, new episode `268437240` was returned,
readback succeeded, and `zune_brand` accepted a 214,600-byte show JPEG. The app
counted one podcast and zero music tracks, cleared successful queue entries and
finalized/released. A subsequent connection found the same exact episode and
excluded it before queue insertion without an upload or mutation. No existing
media was purged. The user subsequently confirmed podcast audio and visible
images work on this Classic. Resume and HD remain unverified.
Sanitized evidence: `tests/fixtures/spoken-media/keel-app-podcast-sync.txt`.

## Native audiobook lead recovered, 2026-10-09

The removed Xune `LibraryManager` implementation at commit
[`3c62dec11ba7`](https://github.com/xune-software/XuneSyncLibrary/commit/3c62dec11ba7)
delegated audiobook creation to its AFTL fork. The submodule pin in that commit
did not yet contain the implementation. AFTL commit
[`04b0f447e116`](https://github.com/magicisinthehole/android-file-transfer-linux/commit/04b0f447e116)
contains the underlying protocol sequence:

- MP3 payload with MetaGenre `0xDC95 = 18` (UINT16).
- Book grouping string `0xDA9A`, author `0xDC46`, part title `0xDC44`,
  part number `0xDC8B` (UINT16), duration `0xDC89` (UINT32).
- Files below `OverDrive/<book>`. No abstract music album or artist creation;
  no SetObjectReferences. Firmware is expected to derive audiobook records.
- Representative-sample artwork belongs to each track.

This is a protocol reference, not copied implementation or proof on our device.
The current Keel MP3 descriptor probe advertises those core fields with the
expected writable types ([sanitized report](../tests/fixtures/spoken-media/keel-mp3-descriptors.txt)).
It marks AlbumName/AlbumArtist read-only, so the planned isolated test will omit
those redundant music-oriented properties. This was the pre-write research state;
the subsequent hardware gate is recorded below. Descriptor
acceptance alone does not establish native category, ordering, playback or resume.

## Native audiobook hardware and app integration, October 9

`zunetool audiobook-test` sent two independently generated 45-second MP3 parts
with a 240×240 JPEG. Both Keel and Pavo returned schema `0x12`, exact author,
part title, book name, part number and duration. Audio and artwork extracted
byte-for-byte; the pre-existing wildcard inventory was preserved. No abstract
music artist/album or SetObjectReferences was used.

Both models normalize MetaGenre 18 to zero on readback. The first HD attempt
rejected that normalization and removed its known newly created objects. The
next attempt applied the schema-qualified Pavo rule and passed. No general
all-model relaxation was added.

Keel retained folder `83887863`, parts `301991678` / `301991679`; the user
subsequently reported “That worked.” This is device acceptance of the small test,
not a separate resume or long-file validation. HD retained folder `83887509`,
parts `301991996` / `301991997`; its screen/playback acceptance remains separate.
Sanitized evidence: [Keel](../tests/fixtures/spoken-media/keel-audiobook-roundtrip.txt),
[HD](../tests/fixtures/spoken-media/hd-audiobook-roundtrip.txt).

The GUI integration adds public `zune_find_audiobook_part` and
`zune_smuggle_audiobook_part`. These use content-keyed folders under OverDrive,
live metadata/order checks and same-name foreign-book conflict refusal. The app
uses independent audiobook queue payloads, off-thread source hashing, immutable
verified copies, separate counts and per-part covers. Chaptered inputs are
explicitly refused pending chapter-preserving preparation; local playback remains.
See [usage](SPOKEN_MEDIA.md#audiobook-status-and-decoded-fields).


### HD inventory trap and application repeat gate

The real app-object gate created part 301991998 under folder 83887510, then its
post-write lookup failed: global MP3 enumeration returned 278 music objects but
omitted the book part; MP3 enumeration scoped to that folder returned zero.
Wildcard enumeration returned 438 objects and included the exact book part.
ObjectInfo confirmed MP3 format, expected folder and byte size. The library now
uses wildcard inventory followed by native schema 0x12 filtering for book lookup.
A software regression asserts that enumeration contract.

After correction, new-session app admission excluded that existing first part
and uploaded second part 301991999. Typed completion counted one audiobook part,
zero music tracks, cleared the queue and released the device. Another connection
excluded both parts without uploading. No pre-existing user content was purged.
Sanitized [app evidence](../tests/fixtures/spoken-media/hd-app-audiobook-sync.txt)
records those results. The failed first attempt did not reach its cover step;
that detail is separate from the successful standalone two-part artwork gate.


Follow-up readback verified both app-test audio parts byte-for-byte. Part two's
app-sent JPEG also matched the prepared cover exactly. The failed first attempt
had stopped before artwork; after verifying its exact known ID, identity and
audio, the local test tool attached its missing cover and verified exact JPEG
readback. This repair touched only that newly created test object. Both test
parts remain available for device-screen/playback checks.


## October 9: Pavo podcast normalization and cleanup outcome

The user's HD GUI transfer uploaded all 19 audiobook parts, then rejected
Moonbound episode "01 Chaotic Beginnings" during metadata verification.
Episode handle 268437588 has native podcast schema 0x10. Its SeriesName 0xDA9A
read empty and MetaGenre 0xDC95 read zero, matching the existing Keel normalization.
These were the only property mismatches recorded. The library confirmed deletion
of the new episode; the application incorrectly retained an unknown-write warning.

The verifier now applies the same narrow schema-0x10 normalization to Keel and
Pavo, while still verifying series relationship, source URL, title, format and
size. Unknown models and ordinary music schema remain excluded. Mutation return
ZUNE_PODCAST_FAILED_CLEANED (-2) distinguishes confirmed new-object cleanup from
ordinary -1 failures, including unknown-ID failures. No retry is implicit.
Software fixtures exercise both families, reuse and lookup, incorrect titles,
wrong schema and unavailable series links. A new HD transfer/playback/artwork
check remains pending; the observed failed transfer is not a successful HD gate.

## Podcast listening-state investigation, 2026-10-09

Purpose: determine whether optional podcast retention can distinguish finished
from partially played episodes on a connected Zune. This investigation does not
enable deletion or change any device media.

The existing `zune_get_podcast_episodes` API exposes inventory, not listening
state. The production ZMDB parser handles music/video/photos but does not yet
publish podcast schema 0x10 state. Do not use `zune_get_track_state` for this
policy: it substitutes zero for unavailable individual reads and can report
success when only another requested field was readable. Missing state must remain
unknown. Play count alone is not a validated podcast completion signal.

A useful lead is already present in the reference inspected earlier: Xune's
[pinned Classic parser](https://github.com/xune-software/XuneSyncLibrary/blob/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/zmdb/ZuneClassicParser.cpp)
and [pinned HD parser](https://github.com/xune-software/XuneSyncLibrary/blob/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/zmdb/ZuneHDParser.cpp).
Credit that project for these candidate field interpretations. No C++
implementation was imported into libzune.

| Podcast record field | Classic v2 offset | HD v5 offset |
| --- | --- | --- |
| Series reference, UINT32 | 0x04 | 0x04 |
| Duration ms, UINT32 | 0x08 | 0x08 |
| Candidate bookmark ms, UINT32 | 0x0C | 0x0C |
| Publication FILETIME, UINT64 | 0x10 | 0x10 |
| Candidate flags, UINT32 | 0x1C | 0x20 |
| UTF-8 title | 0x20 | 0x24 |

Their candidate marked-played mask is `0x200`. Other bits have unrelated
meanings; a nonzero flags word is not equivalent to played. Even a validated
marked-played flag would represent the device's state, not proof every second
was listened to. Natural completion, seeking near the end, manual mark-played
and replay behavior need separate observations.

### Live HD baseline

With Zuuned closed, `zunetool zmdbdump` authenticated to the connected
045e:063e HD and saved `/tmp/zuuned-podcast-state-20261009/hd-baseline.bin`.
Only session setup, database read and session close were performed. No media
writes, cleanup/finalize, resets requested by the investigator, or deletion.
The dump contains 270,380 bytes including the 12-byte transport header; its
reported size matches the file length, ZMDB magic and v5 header are valid, and
index/target-record bounds were checked. SHA-256:
`abc67f814c6965dc65e8399118331d071b2677e6857036f83713353f9e4348d7`.
Raw dumps and session logs stay local and are not added to the repository.

| Episode | Atom ID | Duration ms | Candidate bookmark | Candidate flags |
| --- | --- | --- | --- | --- |
| 01 Chaotic Beginnings | 268437589 | 1573235 | 0 | 0x00000000 |
| 02 The Op | 268437590 | 1927717 | 0 | 0x00000000 |
| 03 Like Black Smoke | 268437591 | 1201095 | 0 | 0x00000000 |

All three reference series 251660372. Their titles and durations support the
candidate structural layout. Zero values alone do not establish the meaning or
liveness of the bookmark and flag fields. Playback history has not yet been
correlated with this baseline.

### Controlled comparison still required

1. Disconnect, play about 30 seconds of the third episode, pause and leave it,
   reconnect with Zuuned closed, and save another complete snapshot.
2. Compare exact atom/series identity, duration and record bytes. Check whether
   the candidate bookmark moves while the marked-played bit remains clear.
3. Let an episode reach its natural end, reconnect, and compare again.
4. Separately check replay, manual played/unplayed if offered, and power-cycle
   persistence. Repeat on Classic before claiming cross-model behavior.
5. Only after those gates, expose a typed libzune state read with explicit
   known/unknown fields and use it in an opt-in app retention policy.

`zune_dump_raw` currently can return success after a short remaining bulk read.
Therefore never accept its return code alone: independently validate the stored
length and structure before comparing. A partial/corrupt read must never
become an empty library or a deletion decision. The offline research inspector
`tools/inspect-podcast-state.mjs` reports candidates only and cannot access USB.

Per-show episode limits/order can be implemented independently using feed
publication dates, stable identities and download availability. Played-state
cleanup should apply only to exact app-managed device copies, preserve explicit
keep choices and keep a local completion record so refresh does not immediately
re-add an episode removed by the policy. Local file cleanup is a separate choice.

Offline inspector verification: 34 synthetic checks pass for Classic/HD layouts,
non-played flag bits, truncated/invalid containers, duplicate IDs, bad bounds
and malformed titles. The live baseline also passes the inspector (504 index
entries, three podcast records). Run from libzune:

```bash
node tests/podcast-state-inspector.mjs
node tools/inspect-podcast-state.mjs /path/to/snapshot.bin
```

This is development-only Node tooling, not an application runtime dependency.
Its JSON reports raw candidate fields and never authorizes a media deletion.

### HD partial-play comparison

After the user reported playing an episode, the HD reappeared as USB device 019
(previously 018). With Zuuned closed, a second read-only dump was taken at
2026-10-10 00:58:53 UTC (October 9 local time). Evidence:
`/tmp/zuuned-podcast-state-20261009/hd-after-play-20261010T005853Z.bin`.
The offline inspector accepts the complete 270,384-byte v5 container, 504 index
entries, and three episode records. SHA-256:
`cf506ab4fe17e126b3ada79ace89a8bab1be552270c3fd3266bd5d2768d202c1`.

- `03 Like Black Smoke`, atom `0x10000857`, duration 1,201,095 ms:
  bookmark changed from 0 to **275,759 ms (4:35.759)**. Flags remain zero.
- The other two podcast records are byte-for-byte unchanged.
- Comparison used atom, series, filename reference, title, duration and record
  size, not raw database offsets (record locations moved after reconnection).
- Within the third episode record, only bytes +0x0C, +0x0D and +0x0E changed;
  all belong to the candidate UINT32 bookmark field. No other record bytes changed.

This corroborates persisted HD partial-play progress at +0x0C after actual
playback/reconnection and shows that this partial playback did not set the
candidate marked-played bit. The user has not yet confirmed the displayed stop
position. Natural completion, seeking versus uninterrupted playback, replay,
manual played/unplayed changes and Classic behavior remain unverified. No
completed episode was observed and nothing was deleted or written to media.

### HD reported-completion comparison

The user replied “done” to the instruction to let an episode finish and reconnect.
The subsequent authenticated, read-only Pavo dump at 2026-10-10 01:15:22 UTC
(October 9 local time) passes the offline inspector: 270,408 bytes, v5, 504 index
entries, the same three podcast identities. Local evidence:
`/tmp/zuuned-podcast-state-20261009/hd-after-finish-20261010T011522Z.bin`.
SHA-256: `fa699ca2f86fa50d2dbe71a7f999ca26cf6adc2b4f74f141160ebae04ef8fd90`.

| Episode | Previous bookmark ms | New bookmark ms | Previous flags | New flags |
| --- | --- | --- | --- | --- |
| 01 Chaotic Beginnings | 0 | 0 | 0x00000000 | 0x00000000 |
| 02 The Op | 0 | 9226 | 0x00000000 | 0x00000000 |
| 03 Like Black Smoke | 275759 | 0 | 0x00000000 | 0x00000200 |

The third episode now sets exactly the reference marked-played mask while its
bookmark resets to zero. This is the first local hardware corroboration of the
HD podcast marked-played bit after user-reported completion. Episode two retains
9.226 seconds of progress without that bit; no inference about how it started
(manual selection or automatic progression) is established. Episode one remains
byte-for-byte identical. No podcast IDs disappeared.

Comparison preserves atom, filename/series references, title and duration.
The third record grew from 658 to 674 bytes. Besides the bookmark and flags,
UINT16 at +0x1C changed from 0 to 1 and the variable record gained 16 bytes;
these additional changes are recorded as unknown, not asserted to be a play
count or timestamp. Codec +0x1E stays 0x3009. A later investigation may decode
them independently. In particular, a zero bookmark can now mean either a fresh
or a completed episode; retention must inspect validated state rather than
using bookmark zero as an unplayed test.

No media was written/deleted and no automatic retention was enabled. This is
one HD sequence with user-reported completion, not independent observation of
uninterrupted playback. Replay/manual mark semantics, repeated completion,
power-cycle persistence and Classic remain separate gates. The next native API
can expose exact raw/known state with these limits; deletion policy still needs
explicit opt-in and exact managed-episode identity.

### Classic post-play observation and inspector correction

After the user tested the Classic and reported “done,” an authenticated read-only
Keel (Zune 30, USB 045e:0710) dump was captured at 2026-10-10 04:30:03 UTC
(October 9 local time). Zuuned was closed. Local evidence:
`/tmp/zuuned-podcast-state-20261009/classic-after-play-20261010T043003Z.bin`.
The complete v2 transport container has 348,820 bytes, 1,489 index entries and
seven podcast records. SHA-256:
`0300bd24f8bd6433d565df12df53cf41361ab4aa6868c250727a9a94562c5a6a`.

The first inspector rejected this intact snapshot because it reserved space for
96 descriptors before allowing the index. The actual Classic snapshot has 33
contiguous ZArr descriptors ending at 0x2CC, followed by the index at 0x2D0.
The HD baseline has 97 descriptors ending at 0x7CC and index at 0x7D0. The offline
inspector now walks bounded, complete contiguous descriptor headers rather than
assuming a fixed HD-sized table. Production C and device behavior are unchanged.
Synthetic fixtures now exercise both actual table layouts. All 35 checks pass,
and both the real Classic snapshot and prior HD completion snapshot validate.

| Episode | Atom ID | Bookmark ms | Raw flags / candidate marked played |
| --- | --- | --- | --- |
| EXODUS - MoonBound - Trailer | 0x100006F7 | 0 | 0x00000000 / false |
| 13 Freedom Tastes Good | 0x100006F8 | 90771 | 0x00000000 / false |
| 01 Chaotic Beginnings | 0x100006F9 | 40599 | 0x00000200 / true |
| 02 The Op | 0x100006FA | 0 | 0x00000000 / false |
| 03 Like Black Smoke | 0x100006FB | 0 | 0x00000000 / false |
| 04 Moon Mites | 0x100006FC | 0 | 0x00000000 / false |
| 05 Into the Hole | 0x100006FD | 0 | 0x00000000 / false |

This corroborates readable candidate state at Classic offsets +0x0C (bookmark)
and +0x1C (flags), including a nonzero bookmark coexisting with the marked-played
bit. It is not a Classic before/after transition: no matching pre-play Classic
baseline was captured. The user was asked whether episode one was completed
then restarted, manually marked played, or only partially played. Until that
history is clarified, do not call its bit proof of natural completion, infer
replay semantics, or assume a played episode always has bookmark zero.
Episode thirteen has 1:30.771 stored progress without the flag; its playback
history was not supplied. Nothing was written to device media or deleted.

Classic user clarification: the user subsequently reported being away for about
two hours and that playback finished, while being unsure of the exact sequence.
This corroborates the observed Classic marked-played flag against user-reported
finished playback. It does not explain the remaining 40,599 ms bookmark or prove
whether replay, automatic progression or another action occurred. Preserve the
independent raw bookmark and marked-played fields. Do not require bookmark zero
or near-duration progress to recognize the device's marked-played state. The
absence of a Classic pre-play snapshot and uncertainty about the sequence remain
limits; no automatic deletion or replay-policy behavior is certified by this test.


### Native read-only integration, 2026-10-09

The observations above now back `zune_get_podcast_states` and
`zune_free_podcast_states`. See [integration contracts](SPOKEN_MEDIA.md#persisted-podcast-listening-state).
The public fields preserve raw bookmark/flags independently, and unsupported or
incomplete snapshots fail without partial publication. The pure C parser accepts
all four existing saved research dumps, including the 33-descriptor Classic and
97-descriptor HD. This checks parser compatibility with the captured evidence;
no new USB read, cleanup or deletion was performed in this implementation gate.

108 sanitizer fixture checks cover parser bounds/overlap/Unicode and fake USB
reads, including rejection of a 94-percent-complete snapshot. The old shared
music-scan transport accepted payloads once at least half arrived; it is now
factored into `src/zmdb_io.c` and requires the entire declared payload. This
strictness is essential before any consumer could use state for retention.
Raw dump tooling remains separately validated by the offline inspector.


Bulk cleanup identity follow-up uses `zune_get_podcast_episode_sources` to read
exact feed/source identities once per device inventory, replacing a repeated
`zune_find_podcast_episode` call for every local episode. The 74-check podcast
protocol fixture proves a shared folder is enumerated once, including multiple
shows, while preserving strict ambiguity/metadata refusal. State-to-live joins
also require the owned ZMDB title and duration to match current MTP inventory;
item/series IDs alone cannot safely correlate reused handles.

Integrated API Classic read-only check, October 9 private candidate: the typed
listening-state, episode inventory and bulk exact-source APIs each returned seven
rows successfully. Played object `0x100006f9` matches across all three by item,
series, parent, title and duration (1,573,235 ms), with bookmark 40,599 ms. No media
writes or deletion were performed. Local evidence is
`/tmp/zuuned-podcast-cleanup-readonly.log`; this supplements saved-snapshot parser
checks without certifying the new application's cleanup flow.

## Keel title-derived podcast filename, 2026-10-10

The real Nontendo #190 transfer completed its 154 MB payload but failed readback
on ObjectFileName (0xDC07). An isolated two-second MP3 with the exact same title
confirmed a 78-byte returned value:

`What the HECK is happening with the PS5 right now? | Nontendo Podcast #190.mp3`

Keel preserves title punctuation in this metadata, including `?` and `|`. The
outgoing basename validator incorrectly rejected the firmware-generated value.
This was not a size/length limit or USB transfer failure. The pre-fix test failed
in the same place, confirmed deletion of its new episode, removed its own show
and folder, and restored the baseline wildcard inventory.

Native schema-0x10 readback now also accepts an exact `Name + .mp3` derivation
on Keel/Pavo, with valid bounded text and no control characters. Outgoing
basenames remain strict. The returned value is opaque device metadata, never a
host path or an episode identity. Other returned safe basenames retain the
previous behavior. Series handle, source URL, Name, other metadata, MP3 format
and size checks remain intact. Unrelated malformed names, wrong extensions and
wrong title readback still fail and preserve new-object-only cleanup.

The post-fix Keel test passed creation, repeat reuse, byte-for-byte extraction,
cleanup of all three test objects and restoration of all 1396 baseline wildcard
handles. [Sanitized evidence](../tests/fixtures/spoken-media/keel-podcast-title-filename.txt).
The fixture uses a generated tone, not the original 154 MB episode; real episode
playback remains a separate user test. Pavo behavior is software-covered here,
not hardware-certified by this Keel test. The 88-check podcast gate passes under
ASan/UBSan, including punctuation, Unicode, long titles and negative cases.

`zunetool podcast-test <short-mp3> [episode-title]` permits an explicit title for
this isolated test while retaining baseline inventory and owned-object cleanup.
Filename mismatch diagnostics print bounded escaped metadata only, never URLs
or authentication data.

The subsequent real Zuuned GUI sync also passed the original Nontendo #190
154 MB payload, then continued through all 19 queued podcasts. Logs show 19
payload sends and 19 artwork completions with no filename mismatches; no podcast
entries remained pending/failed when the app began the separate 49-video phase.
Evidence: `/tmp/zuuned-filename-fixed-app.log`. This confirms actual episode
transfer/metadata verification and queue continuation, not device playback.
