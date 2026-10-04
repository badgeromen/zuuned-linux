# Library location badge tests

Run the pure identity-index cases without building the app:

```sh
bash tests/location/run-index.sh
```

After a native app build, run the real service and QML-binding integration:

```sh
node tests/location/run-backend.mjs
```

The integration runner reuses the app's compiled objects with a fixture
`main`, creates isolated temporary configuration/data/cache roots, and
never constructs DeviceService or starts the app UI. It tests a real
metadata edit, a generated local WAV discovered through the scanner, and
folder deletion. All fixture files and executable outputs are temporary.
Node uses built-in modules only. An optional argument selects a different
Ninja build directory.

The QML contract is `LibraryService.localTrackIdentityRevision` plus
`LibraryService.hasLocalTrack(artist, album, title, discNumber, trackNumber, discReliable, trackReliable)`. Read the revision in
the membership binding so scan/import/edit/delete changes re-evaluate it.
The service rebuilds its hash index once per library refresh; each lookup
takes average constant time relative to library size and does no file or
database I/O. Revision notifications only occur when membership changes.

Matching requires title, album, and either the local artist or albumartist,
compared after trimming and Unicode case folding. Known conflicting disc/track
values reject a match; missing values remain unknown, and multiple compatible
identities suppress the badge. Equivalent format copies share one membership.
Empty fields and the scanner's `Unknown Artist`
and `Unknown Album` placeholders do not support a confident badge. No fuzzy
matching or title-only fallback is used. This means **indexed in your local
library**, not verified identical audio, file format, bytes, or currently
mounted storage. `LibraryService.musicIdentityPeers(track)` returns the indexed source peers
used by device deduplication and badges; it does no per-row library scan.
See `tests/music-dedup/README.md` for the shared identity and migration gates.
