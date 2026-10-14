# Podcasts and audiobooks: integration guide

Status: 0.2.0 prerelease preparation, updated October 14, 2026. These experimental
interfaces are present in this source revision. See the
[API reference](API_REFERENCE.md) for signatures and
[research record](SPOKEN_MEDIA_RESEARCH.md) for evidence and model limits.

## Start without Zuuned

These APIs are usable from a standalone C program. The Qt application is not a
build or runtime dependency. Start with the [buildable C examples](../examples/README.md)
for read-only inventory, prepared podcast upload and prepared audiobook-part
upload. Follow the main [build instructions](../README.md#build), configure
[authentication](BUILD_WITH_MTPZ.md) and USB access, then start with inventory.
The send examples write to the connected device; run only one device manager.

The examples demonstrate integration and error handling. They are not RSS
clients, media converters, automatic cleanup agents, or playback certification.
Use already prepared MP3 files for send examples; an M4B renamed to `.mp3` is
not a converted file. The library does not inspect the codec in these send APIs.


## What works

| Feature | Current status |
| --- | --- |
| Podcast show inventory, creation and exact-feed reuse | Implemented in libzune; tested on Classic / Keel |
| Prepared MP3 episode upload, identity lookup and repeat reuse | Implemented; hardware upload, extraction and repeat admission tested |
| Podcast show covers | `zune_brand` on the series ID; user confirmed visible artwork and audio playback on the tested Classic |
| Podcast firmware listening-state reads | `zune_get_podcast_states` exposes bookmark and marked-played state on Keel/Draco/Pavo; saved-state and read-only device evidence are documented below |
| Podcast cleanup policy | Implemented by the app, using exact source/state correlation and explicit Sync; the library never deletes merely because state was read |
| Device resume control | No podcast/audiobook bookmark-writing API; firmware resume and replay semantics remain separate gates |
| Video podcasts | Not implemented by the podcast sender |
| Native multipart audiobook transfer | Experimental lookup/send APIs for Keel, Pavo and Draco; two-part audio/art readback passed, user accepted Classic test. Resume and long books remain unverified |
| Local audiobook import, chapters, covers and playback | Implemented in Zuuned's app services, outside libzune |
| HD artist backgrounds | Deferred; different from podcast or book cover images |

## Ownership and responsibilities

libzune performs device I/O. The application handles RSS parsing, HTTP downloads,
feed/GUID persistence, codec inspection/conversion, cover selection and local
listening state. Passing an enclosure URL to libzune stores identity metadata;
it does not download the URL. Do not run music duplicate matching on podcasts.

Use one worker/serializer for all operations on a device, including inventory,
artwork and finalization. Calls block. Finish the active call before severing the
session. Standalone `zunetool` commands require exclusive device ownership too.

`zune_breach()` can return a handle without successful MTPZ authentication.
Podcast inventory, lookup and write APIs explicitly reject such sessions. A
failed query must not be interpreted as an empty library. Authentication setup
is described in [BUILD_WITH_MTPZ.md](BUILD_WITH_MTPZ.md).

Podcast episode inventory and bulk source maps accept Keel, Pavo and Draco.
Audiobook inventory/lookup/send also accept Draco following the October 13
small multipart roundtrip. Podcast listening state accepts Keel/Draco v2 and
Pavo v5. Podcast series inventory/creation and single-episode lookup/send use
live property checks, with observed native normalization on Keel/Pavo/Draco.
The Draco podcast normalization correction follows the October 13 Zune 120
failure readback; corrected podcast transfer, playback and artwork still need
a physical retest. This is separate from the successful Draco audiobook
roundtrip and read-only podcast-state baseline.

## Read the podcast library

This complete example only inventories series after connection:

```c
#include "zune.h"
#include <stdio.h>

int main(void)
{
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) return 1;
    ZunePodcastSeries *series = NULL;
    int count = 0;
    int result = zune_get_podcast_series(dev, &series, &count);
    if (result == 0) {
        for (int i = 0; i < count; ++i)
            printf("%u: %s (%s)\n", series[i].item_id,
                   series[i].name, series[i].author);
    } else {
        const char *error = zune_get_error();
        fprintf(stderr, "Podcast inventory failed: %s\n",
                error ? error : "unknown error");
    }
    zune_free_podcast_series(series, count);
    zune_sever(dev);
    return result == 0 ? 0 : 1;
}
```

Build the library first, save the example as `podcast-list.c`, then link on Linux:

```sh
cc -std=c99 -Wall -Wextra -Iinclude podcast-list.c ./libzune.a \
  $(pkg-config --libs libusb-1.0 libgcrypt libavformat libavutil) -o podcast-list
```

The returned array and its strings belong to the caller until released with
`zune_free_podcast_series`. Success with a NULL array and count zero means empty;
failure returns no partial inventory.

## Browse episodes and audiobook parts

`zune_get_podcast_episodes(dev, &episodes, &count)` returns owned
`ZunePodcastEpisode` rows; `zune_get_audiobook_parts(dev, &parts, &count)` returns
owned `ZuneAudiobookPart` rows. Both require authenticated serialized device
ownership. Podcasts accept Keel, Pavo and Draco; audiobook inventory accepts those same families. Unsupported models fail
explicitly rather than reporting an empty library.

Both APIs read the complete wildcard handle inventory, then select native
podcast schema `0x10` or audiobook schema `0x12`. A format-filtered query misses
HD audiobook objects. Music is excluded. These inventories are not restricted
to objects created by Zuuned or MP3 payloads, and do not establish playback support
for the returned file formats.

Each row has `item_id`, `parent_id`, `duration_ms`, `filesize`, `title` and
`author`. Podcast rows add `series_id` and `series_name`; book rows add
`part_number` and `book_title`. Group episodes by **series_id** and books by
**parent_id**, never by display names. Different folders/series can share a name.
Resolve an empty firmware episode series name through `zune_get_podcast_series`
using the actual series ID; the series ID also carries podcast cover artwork.
Book cover artwork belongs to individual part IDs. Sort book parts explicitly
by their part number; returned enumeration order is not a playback contract.

Return 0 means a complete snapshot; an empty snapshot is NULL/count zero.
Return -1 always leaves NULL/count zero, including a failed read after previous
successful rows. The inventory is bounded to 100,000 handles, rejects duplicate
handles, invalid field widths, malformed UTF-16 strings and incompatible native
category metadata, and stops at the first failed read. Call
`zune_free_podcast_episodes(episodes, count)` or
`zune_free_audiobook_parts(parts, count)` to release the array and every string.
Both free functions accept NULL. This is read-only browsing; deletion must be
an explicit caller action against freshly validated object IDs.

The software-only ASan/UBSan gate is `bash tests/run-spoken-inventory.sh`:
66 checks cover both families, same-title identities, wildcard selection,
normalized fields, every individual read failure, strict text/width parsing,
cancellation, malformed inventories, ownership and empty results. These are
fixture results, not new physical-device browsing acceptance.

## Transfer an episode

1. Persist application identity as feed plus opaque episode GUID. Save the first
   device source URL separately from the feed's current download URL. Enclosure
   URLs can change; silently changing the device identity would create copies.
   Multiple GUIDs using the same source URL require explicit conflict handling.
2. Call `zune_find_podcast_episode(dev, feed_url, device_source_url, &parent,
   &series_id, &episode_id)` before device writes. All three output pointers are
   required. Return 0 means the lookup completed; zero IDs mean absent. A found
   series supplies its actual parent folder. A nonzero episode ID can be shown
   as already on the Zune without adding a failed queue entry. Return -1 blocks
   device writes. A responsive app can queue cached intent first and do this
   lookup during sync; Zuuned now follows that model. This lookup does not compare audio bytes, titles or sizes.
3. If the series is absent, create an application-owned folder with
   `zune_forge_folder`, then call `zune_forge_podcast_series` with that folder,
   a safe `.ser` filename, show name, author and exact feed URL. Unlike older
   forge APIs, this function returns zero on success or a negative status and writes the ID through its
   final argument. It reuses one exact live feed match without rewriting it, even if that
   show is in a different folder. A reused ID is not proof the supplied parent
   was selected. Resolve the actual parent with `zune_find_podcast_episode`
   again before sending.
4. For an existing show, use its current device name and actual parent. Read
   the name from `zune_get_podcast_series`; a changed RSS display title is not
   permission to rename the existing show.
5. Prepare a compatible MP3, then populate `ZunePodcastEpisodeInput` and call
   `zune_smuggle_podcast_episode`. Keep the file and input strings alive and
   unchanged until the synchronous call returns. The library maps the file
   read-only and sends it in USB chunks. It does not transcode or sniff codecs.
6. Optionally call `zune_brand(dev, series_id, jpeg_bytes, jpeg_size)` to attach
   the show's cover. Prepare the JPEG in the caller; this API does not resize.
   Report cover failure separately from a successfully transferred episode.
7. After the batch, call `zune_finalize` and then `zune_sever`. Test actual device
   presentation/playback after release; successful USB writes alone do not
   establish either. `zune_finalize` is best effort: its zero return for a
   non-NULL handle does not prove the device accepted every reindex request.

| Episode field | Contract |
| --- | --- |
| `filename` | Safe outgoing `.mp3` basename, not a path; native readback may be exact title + `.mp3`, including punctuation |
| `title` | Nonempty episode display title; not the duplicate identity |
| `author` | Optional author string; NULL is treated as empty |
| `series_name`, `series_id` | Current live show name and its object ID |
| `source_url` | Nonempty stable device source URL retained by the caller |
| `description` | UTF-8 text; NULL is empty |
| `date_authored` | Optional valid `YYYYMMDDTHHMMSS` or `YYYYMMDDTHHMMSSZ` |
| `duration_ms` | Duration in milliseconds from the prepared media |

The file must be regular, nonempty and smaller than 4 GiB. That protocol bound
does not certify long-file playback on every model. The app and development CLI
have smaller limits. Short MTP strings must round-trip through the current UCS-2
codec; unsupported characters/lengths are rejected rather than truncated.
URL/description text uses a different counted UTF-16 array representation.

## Reuse, failures and recovery

The sender repeats live checks. Reuse requires one matching series ID plus source
URL in the supplied parent, compatible MP3 format/size, matching title and podcast
classification. It is not an audio fingerprint comparison. Ambiguous, unreadable
or incompatible matches fail without overwriting existing media.

For both podcast mutation APIs, `ZUNE_PODCAST_FAILED_CLEANED` (-2) means the
operation failed and deletion of its newly created object was confirmed. Keep
the queue failure, but no uncertain-object recovery is needed for that object.
It does not authorize automatic retry. All other failures return -1; zero ID
alone never proves cleanup.

For the two podcast mutation APIs, return -1 with a nonzero output ID means a
newly created object could not be cleaned up. Retain that ID and the device
serial for inspection. Do not blindly retry or delete by title. A lost creation
response can leave uncertain state even when no ID was returned. Folder creation
is a separate caller-owned operation; the APIs do not remove its parent folder.
Inspect `zune_get_error()` and `zune_autopsy()` immediately after a failure.

On Keel, Pavo and Draco, native audio episodes use atom schema `0x10`; MetaGenre may read
back as zero and the redundant series-name field as empty. The implementation
still checks the actual series link, source URL, title, format and size. Keep
these model-specific readback rules; do not extend that normalization exception
to unknown families or non-podcast schemas. Pavo normalization was observed in the October 9 GUI failure;
Draco normalization was observed in the October 13 Zune 120 GUI failure.
Corrected Draco transfer/playback/artwork acceptance still needs a new test.

## Command-line testing

Build with `make zunetool`. These commands query after connection without
creating media:

```sh
./zunetool media-capabilities 0xBA0B
./zunetool media-capabilities 0x3009
./zunetool podcasts
```

Explicit hardware write tests:

```sh
# Short prepared MP3 only: <=30 seconds and <=1 MiB.
# Creates an isolated folder/show/episode, checks reuse/extraction, then cleans up.
./zunetool podcast-test /path/to/generated-short.mp3 [episode-title]

# Retains a prepared MP3 (<=16 MiB) for listening on the device.
# Replace these example URLs and metadata with the real feed/episode identity.
./zunetool podcast-send /path/to/episode.mp3 \
  'https://example.org/feed.rss' 'Example show' 'Episode one' 'Example author' \
  'https://example.org/episode-one.mp3' '20261009T120000Z'
```

`podcast-send` verifies repeat reuse and extracted bytes, then requests reindexing.
It does not send cover art, fetch feeds or automatically clean up retained media.
Use `zune_brand` for covers, as the app does. Neither command proves resume.
Raw standalone authentication logs can contain secrets; keep them private and
share only reviewed, sanitized evidence.

Software-only gates, without a Zune:

```sh
bash tests/run-media-capabilities.sh
bash tests/run-podcasts.sh
bash tests/run-podcast-state.sh
bash tests/run-spoken-inventory.sh
bash tests/run-audiobook-gate.sh
node tools/update-api-reference.mjs --check
node tools/update-toc.mjs --check
```

## Audiobook status and decoded fields

`zune_find_audiobook_part` performs live lookup. `zune_smuggle_audiobook_part`
sends an immutable, caller-prepared MP3, creating identity folders below OverDrive
as needed. Neither creates music albums/artists or writes object references.
Both require serialized, authenticated ownership and currently admit only tested
Keel (Zune 30), Pavo (HD) and Draco (Zune 80/120) families.

Populate `ZuneAudiobookPartInput` with a 64-character lowercase SHA-256 book key,
nonempty book title and part title, a non-NULL author (the empty string is
allowed), a part number in 1..65535 and a positive 32-bit duration in milliseconds.
These requirements apply to lookup as well as upload. Strings must survive the
current short MTP/UCS-2 codec without truncation or character replacement. Zuuned derives the key from book title/author
and the complete ordered sequence of file hashes. Its part number is the reviewed
order, not a guessed disc number. Keep that identity stable across retries.
The library checks the key's syntax but does not calculate or verify its SHA-256
content. A caller must choose and persist its own deterministic book identity;
using a fresh key per part or retry defeats reuse. Use one key for all ordered
parts of one book.

Lookup returns status 0/-1 and writes folder/part IDs through required output
pointers. A zero part ID on success means absent. The sender additionally checks
prepared byte size and MP3 object format when reusing a part. It does not hash
the device audio or compare its bytes with the local file. A different identity folder with the same
book name is refused: the firmware groups by name, so silently accepting it could
merge different editions/authors. Select distinct book names in that situation.

Supply each part as a regular, nonempty, immutable prepared MP3 smaller than
4 GiB; keep the bytes and metadata stable until the synchronous call returns.
The library chooses `Part %05u.mp3` as the wire filename and creates its
`OverDrive/ZUUNED-<book_key>` folders as needed. Those folders are an
implementation detail, not a substitute for native category/identity checks.

Attach a prepared JPEG to **each successfully returned part ID** using `zune_brand`, then
finalize/release after the batch. A nonzero part ID on failed send is retained for
manual inspection; the sender never automatically purges it. A lost creation
response can leave uncertain state even with ID zero. Created folders may remain
following failure. Do not blindly retry, or delete a book by title. A cover
failure after successful audio upload is a separate failure to report or repair;
it does not justify uploading the audio again.

The isolated hardware tool accepts two MP3 files, each <=60 seconds / 2 MiB,
and a JPEG <=1 MiB:

```sh
make zunetool
# Default removes this invocation's known created objects after testing.
./zunetool audiobook-test part1.mp3 part2.mp3 cover.jpg
# Explicitly retain a successful test for device-screen checks.
./zunetool audiobook-test part1.mp3 part2.mp3 cover.jpg --retain
# Software-only argument/readback/API boundary checks; never opens USB.
bash tests/run-audiobook-gate.sh
```

The tool checks metadata, exact audio and cover byte readback, and preservation
of the pre-existing wildcard inventory. It prints retained handles for follow-up.
Do not run it alongside Zuuned. Default cleanup cannot safely remove an unknown
object after a lost creation response; inspect the reported failure first.

### Zuuned application integration

The following behavior belongs to Zuuned, not the standalone C library or its
send examples. In Zuuned, import a book through Audiobooks or Settings → Library. Its **+** or
cover/row drag adds the **whole book**, without a part-selection menu. Existing
matching device parts are skipped during sync. Adding uses cached metadata
immediately; validation, downloads and encoding begin only with sync. Capacity admission covers
all missing chapters together, never a silent partial book.

The app expands embedded chapters into ordered native parts, retaining chapter
names. The first starts at zero, each ends at the next chapter start and the
last reaches the source duration, preserving introductions, gaps and closing
credits. Native FFmpeg decoding trims PCM samples before MP3 encoding; seek
packet boundaries are not chapter cuts. The MP3 Info/LAME header carries encoder
delay/padding. Actual firmware transitions and resume remain hardware gates.

Transfers retain independent book/source/range identities in queue snapshots and
manual-review recovery, never music identities. Large source containers are
copied/hash-verified once per sync source, reused for every chapter, then removed
after their final range. Sources are not modified. Each prepared device part is
limited to 512 MiB; ordinary unchaptered MP3 passes through with original bytes.

The earlier HD app gate proved repeat admission of multipart MP3 across sessions
(before the immediate-queue UX correction): both
existing parts were excluded without uploading. Lookup uses wildcard inventory
because HD MP3-filtered enumeration omits native audiobook tracks. Never substitute
a filtered music inventory for the book lookup.

Remaining gates: full chaptered-book device presentation/playback, long-book
transitions, native resume behavior and other models. DRM is unsupported. Native
range software tests cover boundaries and cancellation; read-only preparation of
an imported long M4B's opening/final ranges also passed. No hardware claim follows
from those software tests.

### Protocol interpretation

Much of the metadata vocabulary was already known: `0xDA9A` (series/grouping),
`0xDC95` (classification), `0xDC46` (author/artist), `0xDC44` (title), `0xDC8B`
(part/track), `0xDC89` (duration) and `0xDC86` (artwork). Recovered external
research supplies the audiobook interpretation of `0xDA9A` and category value
18 (`0x12`). The subsequent Keel and HD tests established native schema `0x12`,
metadata and exact audio/cover readback. Both models read MetaGenre back as zero;
that exception requires native book-track schema, not just an audio filename.

Do not confuse MetaGenre value `0x12` with ZMDB schema `0x12`, or either with an
MTP object format. They occupy different fields. `0xBA05` is a playlist format,
not an audiobook format. See the [property catalog](ZUNE_VENDOR_PROPERTIES.md)
and [dated source evidence](SPOKEN_MEDIA_RESEARCH.md#native-audiobook-lead-recovered-2026-10-09).


## Persisted podcast listening state

`zune_get_podcast_states(device, &states, &count)` reads a fresh, complete ZMDB
snapshot on authenticated Keel (Zune 30), Draco (Zune 80/120) and Pavo (Zune HD)
sessions. Keel/Draco require Classic v2; Pavo requires HD v5. A mismatched
model/version fails explicitly.
Serialize this call with every other device operation. Use
`zune_free_podcast_states(states, count)` to release the result.

Each `ZunePodcastState` contains `item_id`, `series_id`, `duration_ms`,
`bookmark_ms`, `raw_flags`, `marked_played`, and an owned validated UTF-8 `title`. The `marked_played` field tests
exactly flag `0x200`. A nonzero bookmark may coexist with that flag; neither duration nor
bookmark is used to guess completion. Firmware marked-played is distinct from
proof that every second was listened to. Manual mark-played and replay semantics
remain separate hardware observations.

Return 0 means a complete known result, including a legitimately empty snapshot.
Return -1 means unavailable or invalid, always with NULL output/count zero.
Unknown state must preserve device episodes. No mutation or implicit finalization
occurs. A caller's cleanup policy must first match these atom/series IDs against
fresh live podcast identities, including exact title and duration, and its exact
managed copies. Same-series recycled IDs with a different title/duration must
fail the correlation. Reading state alone
never authorizes removal.

The parser validates version 2 or 5, a variable-length descriptor table (33 on
the captured Classic, 97 on HD), complete index bounds and unique identities,
nonoverlapping bounded records, and terminated valid UTF-8 podcast titles.
Model/layout disagreement fails. The shared transport now requires every byte
of the declared payload, including header type/opcode checks and a 64 MiB cap;
older music scans use the same complete-read rule. Raw research `zune_dump_raw`
is a separate path and its saved output still requires offline validation.

Verification: `bash tests/run-podcast-state.sh` passes 108 ASan/UBSan checks,
including partial transfers beyond 50 percent, authentication/cancellation,
model offsets, unrelated flag bits, overlapping records and Unicode boundaries.
The C parser also accepts all four existing local research snapshots: HD
baseline, partial playback, completion, and Classic reported completion. These
are offline reads of saved evidence, not new USB operations or deletion tests.


### Bulk exact source identities

`zune_get_podcast_episode_sources(device, &sources, &count)` provides the exact
`item_id`, `series_id`, `parent_id`, `feed_url` and `source_url` mapping for
cleanup. Free owned strings and the array with
`zune_free_podcast_episode_sources(sources, count)`. Its authenticated active
Keel/Pavo/Draco inventory uses a complete-success/explicit-failure contract.
Source membership alone never authorizes played-episode removal; join the
complete live state and identity inventories before applying retention policy.
It reads series once and each distinct series folder's MP3 inventory once,
regardless of how many local episodes the application follows. The caller joins
local feed/source identities in memory instead of repeating USB searches per
local episode. Parent/format, series link and podcast classification remain
validated. Duplicate feed identities, exact feed/source pairs or object handles
fail the whole read. Same source URL under distinct feeds stays distinct.

The existing single-episode `zune_find_podcast_episode` behavior is unchanged.
The bulk-source-map gate originally passed 74 ASan/UBSan checks, including constant
folder enumeration for multiple shows in one folder, owned Unicode identities,
malformed later rows without partial output, unrelated MP3s, ambiguous identities,
and the observed Pavo schema/MetaGenre normalization. No new live USB test or
hardware deletion is implied by this software gate.

October 10: the Classic title-derived filename correction and its isolated
hardware verification are documented in [the research record](SPOKEN_MEDIA_RESEARCH.md#keel-title-derived-podcast-filename-2026-10-10).


### Zune 80/120 audiobook support, October 13

Draco audiobook inventory, exact lookup and prepared-MP3 sends now use the same
native schema `0x12` path as the Zune 30 and HD. An isolated physical Zune 120
roundtrip passed two 45-second chapters with exact metadata, audio and cover
readback, preserved existing objects and removed only its newly created test
objects. [Evidence](../tests/fixtures/spoken-media/draco-audiobook-roundtrip.txt).
Full-length user books, device-screen playback and resume remain separate checks.
The subsequent listening-state correction below is independently validated;
audiobook success alone does not authorize podcast deletion.


### Zune 80/120 listening-state correction, October 13

The live Zune 120 returned Classic v2 state for 15 podcasts. Every item, series,
title and duration matched the independent MTP inventory and exact feed/source
map. All 15 were unplayed in this snapshot. The previous family guard wrongly
excluded Draco and would also have selected v5 if only its allowlist was changed.
Both checks now match the documented Classic layout.

The parser and played mask `0x200` are unchanged. Native software gates exercise
Keel v2, Draco v2 and Pavo v5, reject every wrong family/version combination, and
preserve failure on incomplete data. Saved physical Zune 30 and HD captures still
parse, including their previously observed played and partial-playback states.
User testing of a completed episode followed by cleanup on Draco remains pending;
the read-only baseline did not delete or modify media. See the [research record](SPOKEN_MEDIA_RESEARCH.md#draco-podcast-listening-state-2026-10-13).
