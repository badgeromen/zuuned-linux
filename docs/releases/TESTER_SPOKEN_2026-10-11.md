# Private spoken-media AppImage test, October 11, 2026

This is a local development snapshot of `feature/podcasts-audiobooks`, including
the private visualizer and uncommitted onboarding, audiobook Libraries and
rounded-artwork changes. It is not a public release.

## Artifact and provenance

- Artifact: `build-appimage/dist/test-2026-10-11/Zuuned-0.1.2-g13ca27d28510-dirty-20261010T082109Z-x86_64.AppImage`
- SHA-256: `629f24aa6f8eadf23aed29ab78d0edcb94d25794958667896742962b1d108cf0`
- App base: `13ca27d28510802ece62bc59ba6beb45eb45533f`, dirty snapshot.
- libzune: `853bf3cde69c09b6d0c752b2288fefb1c88f3d3c`, clean, matching the pin.
- Baseline: Debian 13, Qt 6.8.2, glibc 2.41, x86_64.
- Builder: `sha256:41a9ffba8ef4d91e74d30cb66dac2b96ae11ee1269b88a444fbb55ce2def1c6e`.

The filename timestamp comes from the source commit's `SOURCE_DATE_EPOCH`;
the build and verification happened October 11. Generated metadata, tool lock,
environment description and checksum accompany the artifact. The delivered
checksum was verified after copying.

## Verification

Packaging helper tests, Release compilation and recursive bundled ELF inventory
passed. The third isolated runtime attempt passed both `PASS.txt` and
`photo-builder-PASS.txt`, exercising the actual AppImage with software OpenGL,
null audio, isolated profiles and no USB or network.

Checks covered side-effect-free version reporting, bundled encoding/decoding,
fresh onboarding and Skip, native desktop portal folder/open/save choosers,
FLAC metadata import, playlist import, Appearance persistence, diagnostics
export, private session logging, normal shutdown and SQLite integrity.
The photo gate imported a generated image, created parent/nested albums through
the UI and verified their persistence after restart.

The first two attempts exposed stale Settings click coordinates in the test
harness after adding podcast preferences. Saved captures showed the actual
Music Browse and Photos controls. Only those harness coordinates were corrected;
the AppImage was unchanged across all three attempts. Failed evidence remains.

Evidence is retained under the artifact directory's `evidence/`, including
source provenance and inventory, packaging/build logs, immutable runtime image
IDs, harness hashes, screenshots and
`runtime-3/runtime-WNQDnLkS/{PASS.txt,photo-builder-PASS.txt}`.

Physical audio/GPU behavior, USB transfers, live providers, other desktop portal
backends and older distributions are not certified by these container checks.
This run did not include a copied-profile migration gate. The new spoken-media
GUI and device workflows remain available for user testing in this build.
