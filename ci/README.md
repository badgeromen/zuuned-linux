# Repeatable checks and tester distribution

Run `bash ci/check.sh` from a developer checkout with the README build
dependencies, Node 22+, ripgrep and Qt's QML Test module installed. It builds
the app before linking native fixtures, stops at the first failure, and prints
a unique temporary evidence directory with per-gate logs and source revisions.
Fixtures use generated media/disposable profiles; they never open USB.
The hosted job has a 45-minute limit and echoes successful gate logs into its
job log so detailed output survives removal of the job container. Failed gates
print their last 80 lines; local complete logs remain in the evidence directory.

`.gitea/workflows/check.yml` runs the same entry point. Build
`packaging/containers/Dockerfile.debian13` and register a dedicated runner label
`zuuned-debian13:docker://<your-built-image>` (prefer its immutable digest).
Do not mount a user profile, USB devices or Docker socket into job containers.
The checkout action is pinned; submodules use the committed pin, never remote
branch tip. The runner must have read access to this repository and libzune.
The workflow has no release upload step or publishing token.

Gitea's [runner documentation](https://docs.gitea.com/1.25/usage/actions/act-runner/)
describes label registration. Its
[compatibility documentation](https://docs.gitea.com/usage/actions/comparison/)
describes absolute action URLs. Hosted execution remains unverified until this
workflow is pushed and an appropriately configured runner completes it. The
instance's anonymous version API returned HTTP 403 during preparation; no
server version, artifact API support or upload limit is assumed.

## Release preparation

1. Commit reviewed changes and run the software gates on that exact revision.
2. Build using the Debian container instructions in
   `packaging/containers/README.md`; `packaging/build-appimage.sh` refuses a dirty
   checkout by default and emits the AppImage, checksum, build manifest,
   dependency inventory and environment record.
3. Rehearse that exact artifact with the isolated runtime harness, including
   `ZUUNED_PHOTO_GATE=1` and a read-only copied upgrade fixture. Record
   results in `docs/releases/`; never attach a user's database or settings.
4. Complete the remaining host/device gates in `docs/TESTER_RELEASE.md` and
   label any deliberately outstanding gates in the tester notes.
5. After explicit push/publication approval, create a **prerelease** on this
   repository's Gitea Releases page. Attach the AppImage, checksum, build
   records and concise testing instructions. Verify a tester account can
   download it and check the checksum. Confirm server/proxy upload limits
   accept the actual artifact before inviting the group.

Gitea Releases is the initial distribution target. Repository visibility and
tester membership determine access; configure these explicitly before release.
Keep the previous candidate available for rollback. CI passing alone does not
publish a release or certify physical device playback.
