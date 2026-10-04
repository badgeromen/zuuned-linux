Run `node tests/actions/music/run.mjs` from the repository root.

The suite loads the production MusicPage, its detail pages/cards, TrackListRow,
menus and MusicIdentity helper. Only application service singletons are
replaced with recording stubs. It runs offscreen with isolated data/config
directories and never connects to a device or modifies the real collection.

Coverage includes same-title albums owned by different artists, compilation
ownership, scoped album navigation/save/delete/customization, identity changes
while drilled down, actual alphabet-filtered row playback, nested album payloads,
hero context menus, disconnect during a drag, and independent offline playlist
and playback actions. Device rows lack albumartist, so device album grouping
uses their available track artist.

Collection artwork checks exercise actual genre/mixtape right-click menus,
offline draft cancellation, reactive custom covers on the wall and genre drill,
the saved builder cassette, staged artwork reset, and isolation from device
genres. Run only these with
`MUSIC_TEST_INPUT=tests/actions/music/tst_CollectionArt.qml node tests/actions/music/run.mjs`.
Use `capture-collections.qml` as the input to save native collection-sheet and
builder fixture screenshots under `/tmp/zuuned-collection-*.png`.
Set `MUSIC_TEST_PLATFORM=xcb MUSIC_TEST_RENDERER=opengl` for captures with the
production image masks and gradient heading; the default remains isolated
offscreen software rendering.

The standalone Qt runner has no native GrungeMaskProvider. Its expected
`Invalid image provider: image://grungemask/...` warnings affect decorative
record masks only; the production QML action paths still run.
