# Main checkout tester AppImage — October 1, 2026

Built from the user's approved working main checkout, with no hardening-worktree
changes. No application source was modified, no push/upload performed, and the
running native app and device were left alone.

## Artifact

Directory: `build-appimage/dist/main-2026-10-01-426005d/`

File: `Zuuned-Test-0.1.0-x86_64.AppImage`

Renamed for testers; executable bytes and SHA256 are unchanged. Original build
name: `Zuuned-0.1.0-g426005d74320-20260928T200405Z-x86_64.AppImage`.

SHA256: `6797a10d22144892c5d622e8d0a9384925fae9d8a2bbdd01011226ed310d75c6`

- Application: `426005d74320814f3341d7be8bfced52f854ed90`, clean export.
- libzune: `d29bf7a5492b481f3211fbaeb3c596a00db7c742`, clean export.
- Build baseline: Debian 13, Qt 6.8.2, glibc 2.41; builder image `77be80b0f064`.
- Runtime image: `sha256:b938aa75dcb142a83cc3049b73a1a005c90b97fed7b5605533a248d63e51cf3a`.
- Built October 1; the filename timestamp is the source commit time.

Generated build JSON, environment report, tool lock and SHA256 are beside the
artifact. The copied artifact passed checksum verification. The `evidence/`
subdirectory retains source provenance, packaging tests, build/runtime logs,
runtime image identity, captures and PASS reports. Temporary build inputs are
at `/tmp/zuuned-release-426005d-20261001/source/`; delivery does not depend on
that temporary directory surviving reboot.

## Verification

Packaging helper regressions passed. AppDir inventory and recursive dependency
checks passed for 334 ELF files. The exact AppImage passed the isolated Debian
runtime harness, including side-effect-free version output, bundled media-tool
encoding/decoding, onboarding, native FLAC import with disc metadata,
Original/Halftone rendering and persistence, SQLite integrity, private logging,
actual portal folder/open/save selections, playlist import, Health report export
and normal shutdown. The enabled photo gate passed actual photo-folder import,
staged parent/nested-child album creation and restart persistence. The reopened
parent capture was inspected.

Evidence: `evidence/runtime/runtime-WKYKc4Xb/PASS.txt`,
`photo-builder-PASS.txt`, and `portal-picker-checks.txt` within the artifact
directory. Tests used disposable XDG profiles, Xvfb/software OpenGL and null
audio with no network or USB. No copied-profile upgrade test was run for this
artifact. Physical audio/GPU, host portal integration and device transfers
remain separate gates; the user's successful native-app trial is not a test of
this packaged binary on other hosts.

## Reusable build skill

Installed and validated: `~/.codex/skills/zuuned-appimage/SKILL.md`.
Invoke `$zuuned-appimage`. It defaults to the selected main-checkout commit,
uses the repository's existing snapshot/build/runtime scripts, and preserves
artifacts and evidence outside temporary storage.
