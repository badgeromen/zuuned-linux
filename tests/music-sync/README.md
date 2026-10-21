# Music sync and reverse-import integration

After `cmake --build build`, run `node tests/music-sync/run-backend.mjs`.
The fixture links production native objects with disposable XDG state. It never
constructs DeviceService or opens USB.

The 36 checks cover ordered/repeated playlist members, legacy queue migration,
distinct discs, missing and ambiguous device identity, whole-playlist failure,
same-session send IDs, conflicting identities sharing a path, and reverse imports
whose unknown local metadata must not conflate tracks from different discs.
Pending metadata and its conservative resolution survive reopening the service.
Fifteen byte-level fixtures pin the sync passthrough sniff (`sniffIsMp3`): only
MPEG Layer III audio after any ID3v2 tags is retagged and sent as-is; raw or
tagged ADTS AAC, MP2, ID3-fronted FLAC, WAV, ASF and malformed headers transcode.

These checks establish software decisions, not firmware disc-tag persistence.
