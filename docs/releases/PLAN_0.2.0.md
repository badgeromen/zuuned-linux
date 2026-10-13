# 0.2.0 public release plan

Planning date: 2026-10-13. Preparation approved and in progress. This document
does not authorize publication. The original planning observations below are
historical; both source version declarations are now 0.2.0.

## Destinations and version proposal

- Application: https://github.com/badgeromen/zuuned-linux
  Proposed tag `v0.2.0`, title **Zuuned Linux 0.2.0**, initially a prerelease.
- Library: https://github.com/badgeromen/libzune
  Proposed tag `v0.2.0`, title **libzune 0.2.0**, initially a prerelease.

GitHub currently lists Zuuned 0.1.2 as its newest prerelease. libzune has no
published releases or tags; its local header declares 0.1.0. The proposed minor
version recognizes the new spoken-media features. Version edits have not been
made by this planning pass.

## Scope

Include podcast/audiobook protocol APIs and documentation; native feeds,
downloads, whole-book/chapter preparation, device browsing and sync; retention
and automatic podcast queue preferences; audiobook Libraries and drag/drop;
onboarding, rounded covers, compact-window Now Playing and the plain top-right X.
Include x86_64 AppImage support and an explicitly experimental aarch64 AppImage.

Preserve the existing restriction on public visualizer distribution. The private
branch contains it, so a disabled CMake option alone is not a sufficient source
exclusion. Review/remove private-only visualizer source, assets, dependencies
and references in the public release staging tree without deleting private work.
HD artist backgrounds remain deferred. Keep resume/model-specific claims aligned
with recorded hardware evidence.

## Phase 1: freeze and reconcile source

1. Review and checkpoint the existing work on both private
   `feature/podcasts-audiobooks` branches. The app is currently dirty; libzune is
   clean at `853bf3cde69c09b6d0c752b2288fefb1c88f3d3c`. Do not include unrelated
   local changes merely because they are present.
2. Prepare separate public release branches based on each GitHub repository's
   current `main`. Compare the sanitized export against public changes so no
   contributor work is overwritten. Preserve public history; never mirror the
   private history or force-reset GitHub to a fresh snapshot.
3. Review the public-export filtering before using it for this branch. Keep the
   populated MTPZ header and private history out of public source. Preserve the
   approved provider defaults and external authentication-file override.
4. Review version changes, README, libzune API reference/TOC, setup instructions,
   release notes, licenses/notices and architecture-specific download guidance.
5. Establish an explicit mapping between the private build input and reviewed
   public app/library source revisions. The current public app vendors libzune;
   keep that layout for this release and verify those files match the public
   library revision. Avoid introducing a submodule migration during packaging.

Exit: reviewed release trees, recorded source differences and exact versions.
No dirty-label suppression or unreviewed public uploads.

## Phase 2: verify source and compatibility

Run the native regression suite, focused spoken-media/native, spoken-organization,
device-spoken, libzune protocol/inventory tests, the player-layout matrix,
packaging tests and public-export/authentication checks. Compile the clean public
source without private authentication inputs. Reconcile reported API behavior
with the tests and recorded Classic/HD evidence; software checks alone do not
prove firmware behavior.

Exercise first-run setup and upgrade from a copy of an existing 0.1.2 profile,
including local music/photos/playlists, spoken storage, settings and diagnostics.
Use copies and backups, never the user's live profile as a disposable fixture.

Exit: software gates pass; supported behavior and remaining limits are explicit.

## Phase 3: build release candidates

Use the established Debian 13 container recipes and architecture-specific pinned
tools. Build x86_64 and aarch64 from the same reviewed release input and libzune
revision, with visualizer support excluded from the public candidate.
Trusted official builds retain the existing embedded authentication behavior.

Proposed download names:

- `Zuuned-0.2.0-x86_64.AppImage`
- `Zuuned-0.2.0-aarch64.AppImage`
- `SHA256SUMS`
- Release source and third-party notices, accurately tied to the built artifacts.

Keep tests, captures, private inputs, debug tools and builder outputs out of user
AppImages. Retain detailed build manifests, pinned image IDs and evidence locally.
Publish libzune source/API documentation as a developer release; do not advertise
an unversioned .so as a stable packaged ABI. The app bundles its required library.

Exit: exact candidate files, checksums and source/build provenance recorded.

## Phase 4: validate the packaged files

- Require recursive ELF/dependency checks and clean runtime gates for each
  architecture, including actual portal dialogs and first-run flow.
- Resolve the ARM64 automated onboarding failure recorded in
  `TESTER_ARM64_2026-10-12.md`. Determine whether the app or harness failed;
  preserve failed evidence and do not silently waive the gate.
- Repeat the compact playback checks against packaged Qt 6.8.2 where practical;
  the current eight-screen/304-check gate used host Qt 6.11.2/software rendering.
- Smoke-test the exact x86_64 candidate on the desktop and the exact aarch64
  candidate on the user's Pi. Record OS/architecture, launch, playback and USB
  connection, then scoped podcast/audiobook transfer and cleanup results with
  user-authorized hardware actions. Never erase a device for a release test.
- Seek a Bazzite check for player dismissal and portal behavior. Treat it as
  platform feedback, not something the offscreen matrix already certifies.

The earlier Pi report proves launch and USB setup/reconnect for that older
artifact. It does not certify all playback/transfers for the new build. If ARM
cannot pass its required gates, report it as blocked separately rather than
holding back an otherwise qualified x86_64 candidate without explanation.

Exit: final tested bytes and an accurate known-limitations list.

## Phase 5: review and publish

Prepare the complete source diffs, release notes, source links, checksums and
asset list locally. Present that concrete package for approval. AGENTS.md requires
fresh explicit approval before each push; earlier approvals do not carry forward.

Publish the reviewed library source/tag first, then the app source/tag and its
matching libzune reference. Assemble releases as drafts with all intended assets
before publishing prereleases. Preserve all existing releases. Verify links,
downloaded checksums, --version/About identity, README release buttons and the
source revision recorded for each artifact. Do not rebuild after validation and
upload different bytes under the same version.

No push, tag, release upload or version change occurred during this planning pass.
