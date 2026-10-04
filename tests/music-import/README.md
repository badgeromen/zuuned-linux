# Music import correctness

Run `bash tests/music-import/run.sh` from the checkout. It compiles the actual
scanner, SQLite layer, local track model, transfer queue model and native libzune
probe into a separate temporary executable. FFmpeg generates twenty short local
audio fixtures, including FLAC, MP3, M4A, Ogg and Opus; corrupt-file fixtures
exercise failures. Every database, setting, cache and artwork fixture lives in
temporary directories. No application restart, shared build, provider call or
device access occurs.

The 33 passing checks cover:

- Valid tags, whitespace-only fallback, reliable tagged album ownership and the
  legitimate album title `World 2`.
- Disc tags and separated CD/Disc/Disk markers, year propagation, disc/track
  ordering in SQL, QML snapshots/comparator and the transfer queue. Format
  deduplication distinguishes discs; different library song IDs survive even
  when their titles match.
- Failed new imports stay out of the library. Failed re-probes preserve every
  previously successful metadata/stat/fingerprint field, create a quiet retry
  diagnostic and do not refresh galleries. A successful later scan clears it.
- Completed accessible walks clear retry records for deleted, renamed and
  excluded files. Offline or unreadable watched roots, inaccessible child
  directories, replaced roots and cancelled scans never authorize pruning.
  Nested watched-root ownership takes precedence over a readable parent root.
- Same-size retagging with the exact nanosecond mtime restored is detected by
  ctime. Parser-version repair updates old automatic metadata without requiring
  changed source files. Manual metadata, known disc choices, playback position
  and collection artwork pins remain intact.
- Schema v5 upgrades to v6 without losing rows or choices. Its pre-migration
  SQLite backup includes committed WAL pages, not only the main file.

`node tests/actions/music/run.mjs` also exercises the production MusicPage's
multidisc album playback, queue payload and picker order with disconnected
playback and the existing transfer connection rule. Services in that suite are
recording doubles; this verifies app routing, not USB behavior.

Schema v6 adds disc number, successful-probe timestamps/version and a separate
retry diagnostic table. Parser version 1 requests a one-time re-probe of old
rows during an ordinary scan. Failure never advances the successful fingerprint.
Percent-encoded paths, artwork display treatments and video matching are not
involved. Source media is read only by the scanner; tests retag their own fixtures.

Existing user-edited text fields remain authoritative. Previously unknown disc
numbers may be populated, while known user-edited disc numbers are retained.
Automatic repairs retain album/artist collection identities when custom artwork
or explicit online choices are pinned to those keys. This conservative exception
prevents automatic regrouping from detaching a user's artwork; Sleeve remains
the place for a deliberate collection rename.

The app queues multidisc songs in disc/track order and retains disc/year in
saved queue entries. This change does not add a new USB property or prove
on-device disc-aware deduplication: the device's current identity index still
uses its existing title/artist/album contract.
