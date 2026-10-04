# Feature completion plan

Scope decision, 2026-09-27, retained as the implementation and future-work
backlog. Publishing the current project does not promise delivery dates for
unimplemented phases. Use the release verification checklist in
[TESTER_RELEASE.md](TESTER_RELEASE.md) and each artifact's release notes to
distinguish implemented source from tested downloads.

Device-browser video playback is **out of scope**. Do not add temporary
extraction/playback controls. Saving videos into the library remains supported.
Accurate per-media storage bytes on classic Zunes are **deferred**. Keep honest
counts where sizes are unknown; resume only as a separate protocol investigation,
potentially with Windows wire captures. Neither item blocks this sequence.

## Phase 1 — Custom photo albums and nested albums

Implemented in local source. Folder views remain available alongside virtual albums.

- Persist stable album IDs, parent relationships and ordered photo membership
  in schema v7 through the existing backed-up migration path. One photo may
  belong to multiple albums. An album may hold photos and child albums.
- Create, rename, move and delete albums; reject self-parenting, descendant
  cycles, missing parents and duplicate sibling names. Delete an album and its
  subalbums only after an explicit UI confirmation. Preserve photo rows/files.
- Add existing library photos individually or in batches; remove membership
  separately from removing a photo from the library; reorder members. Rescans
  preserve organization for retained photo IDs. Removing a library photo or
  watch folder removes its membership, but preserves the album tree.
- Browse nested albums with breadcrumbs, empty states and photo viewing using
  existing native components. All organization works offline, without a Zune.
- Queue a selected album's direct photos using its name. Child albums are
  separate selections; do not silently flatten the tree or promise nested Zune
  folders. Existing connected-only queue and media-drag rules still apply.

Gate: isolated v6 migration/backup/reopen checks; tree/cycle/transaction and
membership tests; actual service tests; QML offline create/edit/navigation and
connected-only transfer tests; native build. Hardware photo presentation remains
a separate release gate and must not be inferred from local tests.

## Phase 2 — Disc-aware device duplicate detection

Shared identity, conservative ambiguity handling, audio disc-tag preservation,
and exact-object music/photo recovery are implemented and software-tested.
Schema v8 preserves disc/track metadata in pending playlist slots. See
[`audits/DISC_IDENTITY_2026-09-27.md`](audits/DISC_IDENTITY_2026-09-27.md) for
readback confidence, dropped audio disc tags, all identity consumers and unsafe
legacy title-only recovery. Consult libzune's docs before changing wire fields.
Do not infer that every Zune exposes reliable disc or track numbers.

Implement a shared identity policy preserving disc/track distinctions when
available and a conservative, explicit ambiguity result when device metadata
cannot distinguish two tracks. Preserve old queues through migration. Existing
unknown-disc items must not silently cause either duplicate sends or lost tracks.
Apply the same policy at queue admission, sync-start dedup and interrupted-send
recovery; keep video identity separate.

Gate: same-title multi-disc fixtures, unknown-disc/backward compatibility,
mixed-format tracks, reconnect/recovery and cross-device queues; then a real
multi-disc album send/readback/playback gate on supported devices.

## Phase 3 — On-device metadata and artwork editing

Begin with a capability matrix by media kind, property and device family:
editable name/title/artist/album/genre/year/track/disc, album membership and
representative-sample artwork. Establish supported reads/writes and firmware
re-index behavior using libzune and Windows captures where needed. Existing
rename/video-title repair remains the baseline; unsupported properties must not
be presented as working controls.

Implement protocol support in libzune, then a serialized DeviceWorker operation
with a correlated staged draft in the native UI. Validate/prepare artwork before
USB writes, report actual per-property outcomes, read back changed values and
refresh affected device models/caches. Disconnects and partial writes must stay
visible; do not claim transactional rollback the hardware cannot provide.
Local library metadata/artwork changes require a separate explicit action.

Gate: native validation and failure-injection tests; hardware metadata/artwork
readback, album/artist relationships, eject/re-index and physical display checks
per supported family. Capability research determines the shipping field set.

## Phase 4 — Hardware video encoding

Keep existing NVDEC/VAAPI decoding and software fallback. Inventory output
profiles first: classic profile-0 WMV2/WMAV2 has no assumed NVENC/VAAPI equivalent.
Accelerate only compatible H.264 profiles; never switch a classic device to an
incompatible codec just to use the GPU.

Add runtime encoder capability probing, NVENC/VAAPI encode paths, bounded
software fallback, truthful settings/progress and preserved cancellation/temp
cleanup. Constrain pixel format, dimensions, bitrate, profile/level and audio
to the target device contract, including when decode and encode backends differ.

Gate: available/unavailable encoder fixtures, fallback/cancel/error paths,
ffprobe output contracts and quality/performance comparisons; then actual
playback on a compatible Zune. Classic WMV software encoding stays supported.

## Completion tracking

| Phase | State | Gate evidence |
| --- | --- | --- |
| 1 Photo albums | Implemented with floating builder; software/GPU interaction gates passed | Native build; 29 DB, 73 service, 22 photo + 10 builder QML, 19 playlist DB + 8 playlist UI and 33 music-import checks; packaged hardware gate remains |
| 2 Disc-aware dedup | Implemented; software gates passed, device roundtrip pending | 27 identity, 13 migration, 21 sync/import and 37 recovery checks; 197 audio checks normally and under sanitizers. [Cleanup evidence](CODEBASE_CLEANUP.md) |
| 3 Device editing | Planned | Protocol capability matrix first |
| 4 Hardware encoding | Planned | Encoder/profile compatibility audit first |

Do not mark a phase complete based on compilation alone. Record software and
hardware gates separately. Prepare local changes before any push; every push
requires fresh explicit user approval per AGENTS.md.

September 27 evidence: `tests/photo-albums/README.md` and
`tests/actions/photo-README.md` record repeatable isolated gates. The photo and
builder suites pass both offscreen/software and desktop X11/OpenGL. A 250 px
builder with 5,000 bitmap-backed rows renders a bounded visible set. No running
app was restarted and no user library/device was modified during builder tests.
A new packaged candidate passed the Debian runtime, actual parent/child album
creation and copied v5→v7 profile upgrade/shutdown gates on September 28; see
[`TESTER_PHOTOS_2026-09-28.md`](releases/TESTER_PHOTOS_2026-09-28.md). Physical
photo-transfer verification remains required. Earlier September 9 artifacts
are unchanged.

After those isolated checks, the updated native app was reopened on Photos.
The old session had no media send/in-flight marker and closed normally before
the replacement launch. This startup is not a physical photo-transfer gate.
