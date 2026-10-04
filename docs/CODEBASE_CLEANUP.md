# Codebase cleanup and tester delivery

Approved September 28, 2026. Preserve the existing tested photo candidate;
prepare and verify local changes before requesting any push/publication.

## Order and acceptance gates

1. **Recovery safety and shared music identity.** Replace title-based destructive
   recovery with typed records and exact, correlated deletion. One matching
   policy governs transfers, badges, playlist linking and imports. Preserve
   disc tags in prepared audio and unknown metadata in legacy queues. Schema v8
   retains pending playlist disc/track metadata with a full migration backup.
   Gate: collisions, missing metadata, reliable/unreliable numbers, persisted
   queues, duplicate occurrences, wrong-device recovery, failed completion,
   migration rollback/preservation, and real-media disc tags.
2. **Review and local commits.** Preserve the previously verified photo/provider
   work as a distinct baseline commit, then commit the identity/recovery changes
   with their tests. Never include builds or local profiles. libzune remains at
   the clean, remotely verified `d29bf7a` pin unless a protocol change is needed.
3. **Automated test and release pipeline.** A repeatable local entry point and
   Gitea workflow build/test without USB or user profiles. Release packaging
   identifies exact clean commits, checks all bundled dependencies and retains
   checksums/results. Publishing requires explicit approval; configure tester
   access and server upload limits before the first release.
4. **Gradual structural cleanup.** Extract cohesive responsibilities from large
   service files, beginning with the already bounded artwork HTTP transport.
   Keep behavior and tests intact. Correct stale docs and use shared theme
   tokens in touched UI; avoid an unrelated visual redesign or wholesale rewrite.

## September 28 software evidence

The native build and these isolated gates pass: shared identity (27), v7→v8
migration (13), sync and reverse-import integration (21), interrupted recovery
(37), real-media audio tags (197 normally and 197 with ASan/UBSan), photo service
(73), playlist service (13), photo actions (22), photo builder (10), and local
membership integration. Additional database/import/action gates are documented
with their fixtures. None of these tests uses USB or the user's live profile.

The previously packaged photo baseline is local commit `9b0e25e`; its existing
AppImage remains a schema-v7 development artifact. The newer source uses v8 and
must receive its own package rehearsal before replacing that artifact.

The first structural extraction moves the existing artwork HTTP transport into
`src/library/ArtworkHttp.cpp`. The moved implementation is byte-identical;
request ownership, cancellation, provider pacing and cooldown behavior are
preserved. `LibraryService` keeps gallery orchestration. Further decomposition
of scanning, sync and settings remains gradual follow-up work.

The complete `bash ci/check.sh` run passed on September 28 after the extraction:
29 stages, including configure/build and all registered software suites. Local
evidence is `/tmp/zuuned-checks.ZXZAr9oc/` (individual logs and `results.txt`);
the console record is `/tmp/zuuned-ci-final.log`. Provider shutdown passed all
six checks with a 3 ms worker drain; provider/cache passed 51/21 and the artwork
pipeline passed its full fixture. The code was then captured in clean source
commit `999ce51` (the only subsequent source edit removed a blank EOF line).

Debian builder image: `sha256:41a9ffba8ef4d91e74d30cb66dac2b96ae11ee1269b88a444fbb55ce2def1c6e`.
Schema-v8 runtime harness image:
`sha256:b938aa75dcb142a83cc3049b73a1a005c90b97fed7b5605533a248d63e51cf3a`.
The clean snapshot and pending package log are under
`/tmp/zuuned-cleanup-20260928/`; these image builds alone do not certify a new
AppImage. Hosted Gitea execution and tester download access remain unverified.

## Remaining release limits

No destructive hardware tests on the user's media. Software fixtures and
isolated package rehearsal do not establish device disc-tag readback, physical
playback, photo transfer, USB setup or GPU/audio behavior. Those remain named
hardware gates. Device-browser playback is excluded and classic per-media size
discovery remains deferred.

Existing evidence: [photo candidate](releases/TESTER_PHOTOS_2026-09-28.md),
[identity audit](audits/DISC_IDENTITY_2026-09-27.md),
[recovery fixtures](../tests/recovery/README.md), and
[identity/migration fixtures](../tests/music-dedup/README.md).
