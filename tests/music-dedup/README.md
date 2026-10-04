# Music identity and pending playlist migration gates

Run isolated native fixtures (no app, real library, network or USB):

```sh
bash tests/music-dedup/run-identity.sh
bash tests/music-dedup/run-migration.sh
```

The shared header `src/library/MusicIdentity.h` indexes all candidates under
structured metadata keys. It distinguishes exact, unique legacy, ambiguous and
missing results. Missing disc/track values stay unknown; contradictory known
numbers never match. Source peers prevent a single legacy device row from
silently covering same-title songs on several discs. Artist fallback cannot
contradict a known artist. Normalized album artist is preferred for outbound
identity; the local membership index also stores track-artist aliases.

Readback retains `disc_number` returned by libzune. Classic ZMDB track-number
slots are not treated as reliable identity. Same-session send completion keeps
intended disc/track fields and marks `identityFromReadback=false`; these tests
make no claim that the device persisted those values correctly. Physical
roundtrip validation remains separate.

The identity fixture exercises 27 cases, including collisions, multiple exact
objects, source ambiguity, placeholders, reliability flags, aliases, duplicate
format copies and membership revisions. The migration fixture exercises 13
cases: schema v7 to v8 with a live committed WAL writer, an unchanged v7
rollback snapshot, preserved virtual albums, ordered pending disc/track
metadata, reopening, edit merge and out-of-order slot resolution.

Schema v8 adds `pending_disc_number` and `pending_track_number` to
`playlist_tracks`, both defaulting to zero. The original unresolved metadata
is retained through migration; legacy rows stay unknown. Resolving a slot
clears its obsolete pending fields and uses the referenced local track.

Related regression gates are `tests/location/run-index.sh`,
`tests/playlist-order/run-db.sh`, `tests/photo-albums/run-db.sh`,
`node tests/actions/music/run.mjs`, and
`PHOTO_TEST_INPUT=tests/actions/playlist bash tests/actions/photo-run.sh`.
