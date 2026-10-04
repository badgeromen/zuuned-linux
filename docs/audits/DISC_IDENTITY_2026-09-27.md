# Disc-aware identity audit — 2026-09-27

Status: source/documentation audit complete; implementation and hardware gates
pending. This report combines the protocol/readback and application-consumer
audits. No USB operations, device modifications or new hardware observations
were performed. Existing parser support is not evidence of reliable readback on
every family. Read `libzune/docs/TOC.md` before investigating protocol changes.

## Readback and transfer contract

`libzune/include/zune.h` exposes `ZuneTrack`, used by
`ZuneDBLibrary.tracks`; there is no separate `ZuneDBTrack` type. Relevant fields:

| Field | Current implementation | Confidence/limitation |
| --- | --- | --- |
| `tracknumber` (`uint16_t`) | ZMDB fixed offset +24 in `zmdb_parse_track`, `libzune/src/zmdb.c` | Raw parser output is not always trustworthy: existing `qml/TrackListRow.qml` and `qml/MusicPage.qml` document Keel snapshots returning 2297 on every row. Their UI guard is not a protocol validation rule. |
| `disc_number` (`uint16_t`) | Optional backwards-varint field `0x6C`, zero when absent | Field is documented as uint32; current parser narrows it to uint16. Missing fields, malformed values and truncation must not become confident identity. |
| Disc/track send fields | `zune_smuggle_track_tagged/ex` accepts track number only | No explicit disc argument or proven writable disc MTP property in the reviewed headers/wire records. |

`libzune/docs/ZMDB_FORMAT.md` documents Classic ZMed v2 and HD v5, with
28-byte and 32-byte music fixed sections. Both use the same optional-field
parser; its `0x6C` attribution cites XuneSyncLibrary. The docs cover Zune 80/120
and HD dumps, but do not establish known-disc round trips on every Classic/HD
family. The parser examines at most eight optional fields. Treat zero as unknown,
not disc 1. Positive values still require confidence appropriate to their source.

The alternate MTP enumeration in `libzune/src/track.c` reads standard Track
property `0xDC8B` and leaves disc zero. The Windows music recipe in
`libzune/docs/WIRE_CAPTURE_FINDINGS.md` includes track number, not an identified
disc property. Do not guess a vendor property to make writes appear complete.

**Payload preservation gap:** `transcode/libav_transcode.c`, audio output tag
construction, emits TIT2/TPE1/TALB/TCON/TRCK/TYER but no TPOS. Audio converted
from FLAC/WMA/etc loses disc tags. Existing MP3 rewriting in
`transcode/id3_rewrite.c` preserves parsed frames, including ordinary text
frames, but does not provide a disc override. Preserve disc in prepared media
and test the bytes before claiming reconnect-safe identity. Whether each device
indexes that tag still needs a hardware gate.

## Application consumers

| Location | Finding |
| --- | --- |
| `src/DeviceWorker.cpp`, library read loop | Copies track number but drops `disc_number`; sorting uses track number without disc. |
| `src/TrackModel.h`, `.cpp` | No disc field/role/export. |
| `src/DeviceService.cpp`, `deviceTrackRows`, `noteSyncedTrack`, `rebuildOnDeviceKeys` | Exported identity and badges use artist/album/title. Newly sent rows omit disc, track and duration; their artist can differ from the canonical album artist sent on the wire. |
| `src/sync/SyncEngine.cpp`, `addTracks`, `deviceDedupIndex`, `startSync` | Queue admission and final pre-send dedup collapse identical artist/album/title across discs. Both must use one policy. |
| `src/sync/SyncQueueModel.cpp`, `addTrack` | Local queue already preserves distinct library IDs and disc/track fields; metadata fallback uses case-sensitive artist/title/album/disc/track. Saved entries retain disc/track. |
| `src/sync/SyncEngine.cpp`, pending playlist persistence and playlist phase | Stored full/short keys omit disc/track. Hash insertion overwrites colliding candidates; the resolved-ID `seen` set can then drop a second member. Artist-free fallback can select another artist's recording. |
| `qml/TrackListRow.qml`, individual drag payload | Drops disc/year despite the local row having them. |
| `qml/PlaylistsPage.qml`, member delegate | Overwrites real track number with playlist position in the object used for actions/drag. Display position needs a separate field. |
| `qml/TrackListRow.qml`, `qml/MusicPage.qml` | Track and album badges independently reconstruct coarse keys, potentially marking partial multidisc albums fully present. |
| `src/library/TrackIdentityIndex.h`, `LibraryService` import/playlist matching, `qml/Main.qml` import suppression | Local membership lookup ignores disc/track; first compatible title/artist/album with blank-field wildcards can attach the wrong disc or suppress its import. |

Album-artist canonicalization and disc-folder normalization in SyncEngine are
grouping rules, not proof that two recordings are identical. Preserve them while
keeping identity distinctions. `LocalTrackModel` already carries local disc and
track metadata; it is not itself a matcher.

## Interrupted-send recovery risk

Music `markInflight()` currently writes only title. The non-video branch of
`purgeInterruptedSend()` matches every music track **and photo** by title/stem,
requests deletion of all matches and clears its marker before deletion is
confirmed. This is a source-confirmed risk to unrelated existing media, not a
newly reproduced hardware incident. A multidisc fix must not retain this broad
recovery behavior. Video has a separate filename-based recovery path; preserve
that separation.

Use typed recovery records with full intended identity, device identity,
pre-existing candidate IDs and exact returned item ID where available. Unknown
or legacy title-only markers must never trigger automatic bulk deletion. Keep
the marker until the selected exact deletion is confirmed; ambiguity needs an
explicit non-destructive result.

## Implementation sequence

1. **Shared identity and confidence.** Add a native candidate index retaining
   all candidates per normalized artist/album/title. Include disc/track and
   provenance/confidence, local library ID, device item ID and duration. Return
   exact / unique legacy / ambiguous / missing. Known conflicting numbers never
   match; unknown values never silently become disc 1. A legacy fallback requires
   one compatible candidate and no competing source identities. Duration is
   supporting evidence, not sufficient identity by itself.
2. **Carry identity end to end.** Expose readback disc and confidence, preserve
   real track/disc in drag payloads and saved queues, separate playlist position,
   preserve TPOS through conversion, and retain intended send metadata locally.
   Intended same-session disc metadata must not be labeled device-confirmed.
3. **Apply the policy to every consumer.** Queue admission, pre-send dedup,
   badges, imports and playlist membership share the matcher. Use exact returned
   IDs for same-sync playlists. Do not arbitrarily select ambiguous candidates
   or silently send/skip them. Migrate legacy queue/playlist state safely.
4. **Make recovery conservative.** Implement typed markers and exact,
   completion-correlated deletion. Preserve legacy markers without bulk purge.
5. **Verify reconnect and hardware behavior.** Software gates precede controlled
   multidisc send/readback/eject/playback tests. A tester build may include the
   completed photo work without claiming this later phase is implemented.

Software fixtures must cover same-title discs, repeated tracks, unknown/invalid
numbers, canonical artist aliases, unique versus ambiguous legacy matches,
cross-device saved queues, mixed source formats, old pending playlists and
recovery markers, reconnect, and failures. Extend music-import, location/index,
playlist and music-action tests; use isolated native fixtures without USB for
recovery decisions. Verify converted MP3 disc tags independently of the parser.

Hardware questions remain: which Classic/HD variants reliably expose disc and
track; whether prepared TPOS becomes `0x6C` after re-index; behavior for missing
disc, duplicate titles and mixed formats; and whether known-disc same-session
state agrees with reconnect readback. Any parser/protocol repair belongs in
libzune, with captured evidence and its own tests, not an app-side guessed fix.
