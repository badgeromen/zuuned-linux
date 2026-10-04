# Music sync and reverse-import integration

After `cmake --build build`, run `node tests/music-sync/run-backend.mjs`.
The fixture links production native objects with disposable XDG state. It never
constructs DeviceService or opens USB.

The 21 checks cover ordered/repeated playlist members, legacy queue migration,
distinct discs, missing and ambiguous device identity, whole-playlist failure,
same-session send IDs, conflicting identities sharing a path, and reverse imports
whose unknown local metadata must not conflate tracks from different discs.
Pending metadata and its conservative resolution survive reopening the service.

These checks establish software decisions, not firmware disc-tag persistence.
