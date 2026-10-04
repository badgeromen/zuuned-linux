# Local music copies

The October 2 test library contained local and SMB copies of Zweihänder's
*Ranger of the Old Woods*. File sizes differed because container metadata
differed, while the audio payload matched. Importing by filepath alone showed
each song twice.

## Behavior

The music browser, album and artist counts, playback selections and new
album transfers show one representative of a verified copy group. Every
physical source row and its ID remains in the database. No file is deleted,
renamed, retagged or excluded by this feature. Existing playlists preserve
their IDs, ordering and intentional repeated entries. Artwork discovery still
tries every source, and original rows remain available to metadata editing.
New additions to a playlist recognize existing equivalent source IDs, while
intentional repetitions already in saved playlists remain unchanged.

**Whole-library correction, October 2:** every indexed song receives audio
verification, including unique-looking and poorly tagged files. No album,
artist, filename, duration or track-number prerequisite controls coverage.
Verified audio is the grouping key. Differences in performer, title, year or
reported duration do not defeat recognition of identical audio.

Known full album names and positive disc/track positions protect separate
album memberships. Edition labels are never removed. Missing values can join
an unambiguous compatible group. A file that could belong to multiple editions
or positions remains separate; it cannot bridge those groups. This decision
examines all matching audio peers before grouping, independently of import
order. Identically incomplete copies can still group with one another.
Differing audio stays separate even when every tag agrees.

A representative with more complete album/position information is preferred,
then a normal filesystem path over a GVFS path, then the oldest imported ID.
No metadata is invented or written back to files. Removing a watched
folder leaves its other indexed copy available to the browser. This does not
add automatic source failover to already-saved playlists or active queues.

## Implementation and limits

Schema v9 adds `tracks.audio_fingerprint`, using the existing backed-up
migration. The scanner verifies each indexed source, reusing unchanged cached
results. Full encoded audio packets, codec parameters and codec
initialization data produce a versioned SHA256 fingerprint. Tags and attached
cover images are excluded. The initial verification reads each audio stream;
subsequent scans reuse unchanged results.
Successful results are reused until size or nanosecond file timestamps change.
Pre/post file checks and conditional database updates reject stale publication.

Hashing runs on the scanner worker and observes cancellation. Read failures,
demux errors, corrupt packet flags, empty audio and multiple audio streams
leave sources separate. This is conservative copy recognition, not acoustic
matching or a decoder health check. Different encodings or packetizations of
the same recording may remain separate. Two differently labelled releases
with indistinguishable tags and identical audio cannot be distinguished by
this evidence.

`LocalTrackModel::rows()` remains the raw source list for ID-based consumers.
`displayRows()`, QML roles and snapshots expose the grouped list. Device
identity and the existing optional FLAC/MP3 format preference are separate
policies and are unchanged.

## Verification

- `bash tests/music-import/run.sh`: real encoded fixtures, metadata/artwork
  changes, changed audio, cancellation, multi-stream refusal, cached rescan
  and conditional persistence.
- `bash tests/local-music-copies/run.sh`: identity boundaries, model roles and
  snapshots, raw ID retention, playlist occurrences and watched-folder removal.
- `node tests/customize/run-backend.mjs`: production service counts and saved
  playlist resolution, alongside existing Customize and transfer guards.
- Migration, playlist and photo database gates cover the schema version change.

The first candidate passed 34 groups in `bash ci/check.sh`. Whole-library
coverage adds 52 import checks and 87 display/database checks, including
unrelated albums and every order of ambiguous incomplete metadata.
Production integration also verifies duplicate-aware playlist additions and
artwork discovered only beside an alternate source with different artist tags.

A SQLite backup of the active test library retained all 1,748 source rows.
The first candidate reported 1,282 displayed songs. *Ranger of the Old Woods*
has 28 preserved source rows and 14 verified display groups. The earlier
schema-v8 profile remains untouched. Review profile:
`/tmp/zuuned-copies-review.Tc90zk`; build and evidence:
`build/testing/local-music-copies/`.

The corrected whole-library candidate also passed all 34 regression groups,
then the final ordering and production-service checks. Its copied profile at
`/tmp/zuuned-whole-library-review.iz055b` has verified audio for all 1,748 indexed
sources and displays 1,223 songs. All source rows remain. Native build and
evidence are in `build/testing/whole-library-copies/`; earlier profiles remain
available unchanged.

This is a native development candidate. Packaging and device testing remain
separate from these local import checks.
