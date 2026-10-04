# Local music copy regression gate

Run `bash tests/local-music-copies/run.sh` from the checkout. This compiles the
native model and database in a temporary directory. It does not access user
libraries, media, network providers, or hardware.

The checks cover whole-library verified audio grouping across unrelated albums
and source folders, unequal file sizes, inconsistent artist/title/year tags,
missing metadata, informative representative selection, local/GVFS preference,
stable IDs, missing audio evidence, edition/disc/track separation and all input
permutations of ambiguous incomplete copies. Model roles and snapshots,
persistent physical source rows, repeated playlist entries, and promotion of a
surviving copy after folder removal are also covered.

The fixture fingerprints are already verified inputs to this display policy.
Actual audio hashing and scanner refresh are tested in `tests/music-import/`.
The production `LibraryService` counts and playlist boundary are additionally
covered by `node tests/customize/run-backend.mjs` after building the native app.
No automatic playback fallback from an unavailable preferred file is certified
by this test.
