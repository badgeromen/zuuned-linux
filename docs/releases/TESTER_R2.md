# Tester r2 candidate — September 8, 2026

This historical candidate is superseded by the
[September 9 diagnostics candidate](TESTER_R2_DIAGNOSTICS.md). Its original
artifact and device evidence remain recorded here. Neither candidate has been
publicly published; remaining checks are in [TESTER_RELEASE.md](../TESTER_RELEASE.md).

## Exact artifact

- App: `6dbf632212a1509d16101f0ff944a75b3ae6bdca`, clean.
- libzune: `d29bf7a5492b481f3211fbaeb3c596a00db7c742`, clean.
- File: `build-appimage/dist/Zuuned-0.1.0-g6dbf632212a1-20260909T004931Z-x86_64.AppImage`
  (about 127 MiB; filename timestamp is UTC).
- SHA256: `5fb3eef6eacba2982cd38e5882588acc7b2d5b0f41ffb70fe3a1edf1fc2776d9`.
- Matching checksum, build JSON, tool lock and environment report are alongside
  the AppImage. Curated local evidence is in
  `build-appimage/evidence/tester-r2-6dbf632/` (ignored build output).

The subsequent documentation commit records results without changing the
artifact's source revision. Preserve the complete filename/build ID in reports.

## Build and runtime

The [container recipe](../../packaging/containers/README.md) exports exact Git
objects for clean app/submodule trees. This fresh export built and installed on
the pinned Debian 13 slim baseline with Qt 6.8.2 and glibc 2.41. All 333 bundled
ELF files pass recursive dependency inventory; crypto/audio/USB/C++ runtime
dependencies cannot silently resolve from the builder. Tools and launcher
runtime are checksum-pinned. All bundled symbol requirements reach at most
`GLIBC_2.39`; this does not certify an untested older distribution.

Final runtime evidence: `/tmp/zuuned-r2-runtime-evidence/runtime-1bHQTb1k/`.
Runtime image: `sha256:c31e50895cad1236eb60b675c319488916b9ff0aeeeaf01317fb5eb1917ea33b`.
No system Qt/mpv/FFmpeg, USB, network, host media or desktop sockets were exposed.

The actual final AppImage passed identification/extraction, generated media-tool
encode/decode, onboarding/Skip, native FLAC import including disc metadata,
Original/Halftone Appearance, saved settings, normal shutdown and DB integrity.
A copied real v5 profile migrated to v6 with all 846 tracks, 7,751 videos,
80 photos, seven playlists, 270 membership rows and seven collection overrides
preserved. Its full v5 rollback SQL dump equals the input. Original media/cache
paths remain in the copy but their files were deliberately unmounted; those
local-image warnings are retained and excluded only from the upgrade check.

The same AppImage then opened on the actual Arch/Omarchy desktop via XWayland
with bundled Qt 6.8.2. OpenGL context creation, rendered library/artwork and
device auto-connect were verified; no QML warnings were reported. The live v5
profile migrated to v6 and created `library.db.v5-backup`; 846 tracks remain.
Host log: `/tmp/zuuned-tester-r2-app.log`. Desktop captures stay local.

## Real device rehearsal

The pinned native `zunetool` was tested against the attached **Draco 120 GB**.
The running app's current log, stopped playback and absent in-flight marker were
checked before closing its window normally. Every device command ran serially.
Fixtures were generated with the candidate's bundled FFmpeg.

- Authentication and baseline: 704 tracks, five playlists.
- Two tiny MP3s uploaded, extracted and compared byte for byte.
- Temporary playlist references read back B → A → B, with zero dangling refs.
- Repeated-send test: 20/20 successful uploads.
- Movie/episode title, filename, category, season/episode, size/format and
  title-only rename passed the existing native video test.
- All five CleanDataStore finalizations returned actual protocol success.
  Known OpenSession timeouts recovered through the existing reset/retry path.
- Final track and playlist rows match the original inventory, including playlist
  order. All 22 test track IDs and the temporary playlist are absent. Both test
  video IDs subsequently returned `InvalidObjectHandle (0x2009)`.

Evidence: `/tmp/zuuned-r2-device-6dbf632/`. Its saved owned-object inventory and
read-only `verify.mjs` correlate uploads/readbacks/cleanup; the curated
`hardware-PASS.txt` contains no original library titles. Raw logs stay local.

These CLI checks do not certify packaged-app photo sync, actual firmware
playback or sound through speakers/headphones. The host's existing permissions
also do not prove fresh-install udev ACLs. Keep those hands-on gates explicit
when sharing the candidate; do not advertise untested distro/device models.
