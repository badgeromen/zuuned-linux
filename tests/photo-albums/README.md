# Virtual photo album gates

Run the isolated storage gate with `bash tests/photo-albums/run-db.sh`.
It compiles `LibraryDb` against Qt Core and SQLite and uses disposable `/tmp`
fixtures; it never opens the user's library or accesses a Zune.

The 29 native checks cover the schema v6→v8 migration and its pre-upgrade
SQLite backup (including committed WAL pages), preserved photo IDs/data,
persistent ordered many-to-many membership, stable album IDs, sibling naming,
cycle rejection, subtree moves/deletion, and failed-batch rollback. Edits also
participate in an enclosing transaction. Removing a library photo cascades its
memberships; deleting an album preserves the indexed photos and source files.
All source fixture files are checked after edits/deletions.

Schema v7 gives both photos and albums non-reused AUTOINCREMENT IDs so a stale
picker cannot target a newly created row after deletion. The gate verifies
photo ID non-reuse after migration and album ID non-reuse after subtree deletion.
Hierarchy depth is limited to 32 levels, including the root album: creation and
moves validate the full resulting subtree depth, below SQLite's cascade limit.
Reorder must supply an exact permutation of current members; stale editor lists
fail rather than discarding unseen additions. Adding repeated photos is harmless
and keeps their original positions.

The existing playlist database regression also passes 19 checks at schema v8:
`bash tests/playlist-order/run-db.sh`.

After `cmake --build build -j 4`, run
`node tests/photo-albums/run-backend.mjs` for the real LibraryService gate.
It compiles a fixture against native app objects and opens only a disposable
temporary XDG profile. All 73 checks pass: offline edits, ordered covers and
memberships, ancestor labels, direct-member badge counts, finite/integral ID
validation, truthful errors/signals, atomic create-with-members rollback,
scanner upsert preservation, subtree deletion and service restart persistence.
Draft checks cover atomic name/parent/membership/order saves, rollback, deleted
albums and concurrent name/parent/order/membership changes. A 5,000-photo create
took 138 ms and reorder/move took 47 ms on the development host; these timings
are observations, not hardware-independent limits.

`bash tests/actions/photo-run.sh` passes 22 QML checks, including actual
offline folder/group/gallery/filmstrip drags, bulk selection and draft-aware
plus buttons. `PHOTO_TEST_INPUT=tests/photo-albums/tray bash tests/actions/photo-run.sh`
passes 10 builder checks: dirty replacement/discard, save failure retention,
offline drop, controls, and 5,000-thumbnail virtualization at a 250 px width
(fewer than 40 instantiated list children at both ends).

Both suites also pass using desktop X11/OpenGL:

```sh
PHOTO_TEST_PLATFORM=xcb PHOTO_TEST_BACKEND=rhi QSG_RHI_BACKEND=opengl bash tests/actions/photo-run.sh
PHOTO_TEST_PLATFORM=xcb PHOTO_TEST_BACKEND=rhi QSG_RHI_BACKEND=opengl PHOTO_TEST_INPUT=tests/photo-albums/tray bash tests/actions/photo-run.sh
```

The builder test captures `/tmp/zuuned-photo-tray-narrow.png` using a bundled
bitmap fixture; its thumbnail rows and controls were visually inspected.
The shared music playlist action regression passes 8 checks. The native app builds;
the existing music-import gate passes 33 checks with the v8 migration.
These isolated results do not certify packaged device photo transfers.
