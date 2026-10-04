# Imported playlist order

Run the isolated gates:

```sh
bash tests/playlist-order/run-db.sh
bash tests/playlist-order/run-tray.sh
cmake --build build
node tests/playlist-order/run-backend.mjs
```

The database gate compiles the actual SQLite layer into a temporary executable.
The service gate links the built native app objects with a test entry point and
uses a disposable library and QSettings directory. The QML gate exercises the
production TrayState singleton with a recording library. No gate opens a Zune,
accesses the user's library, or restarts the app.

Current coverage: 19 database assertions, 13 actual LibraryService assertions,
and 6 QML cases (8 QtTest passes including setup/cleanup). This includes schema
v3→v4→v5, correct foreign keys and backups, reverse/out-of-order arrivals,
restarts, repeated occurrences, reactive pending counts, transactional failures,
legacy-note migration, deletion while pending, and edits made while new tracks
arrive. The native service gate also covers repeated M3U entries.

Schema v4 gives every playlist occurrence a stable row ID and position. A
missing imported track has a NULL track ID and retained artist/album/title;
resolution fills that row in place. Playlist and entry IDs use AUTOINCREMENT so
stale import/edit state cannot attach to newly created rows after deletion.
Schema v5 adds the independent collection customization table.

`importDevicePlaylist()` reserves the full ordered sequence in one transaction
before Main queues downloads. Repeated occurrences remain separate entries, but
each missing device item is pulled once. Existing metadata matching semantics
remain unchanged; ordering does not establish file-content identity.

The tray saves stable occurrence IDs plus the set it initially observed. It can
reorder or remove the exact repeated occurrence the user chose, and add new
tracks. Entries it did not observe—pending slots, tracks resolved while it was
open, or external appends—remain in their existing gaps. New members are inserted
before the next explicitly ordered existing member, or appended after the full
sequence. Removing a playlist deletes all pending entries and its mixtape
customization row in the same transaction.

Legacy QSettings notes migrate once with a transactional database marker, so a
crash before clearing QSettings cannot duplicate them. Their stored order and
repeats survive, after the playlist's existing tracks. Earlier versions never
saved their original positions among already-present tracks, and already
scrambled playlists cannot be reconstructed automatically. Reimporting the
original device playlist creates the full correctly ordered sequence.
