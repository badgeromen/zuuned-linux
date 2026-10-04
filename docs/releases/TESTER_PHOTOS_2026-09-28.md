# Photo album testing candidate — September 28, 2026

This development AppImage replaces the September 27 candidate. It includes
custom/nested photo albums, the floating builder, bulk selection, offline local
photo drag/drop, staged Save/Cancel, and the provider-shutdown fix found during
the copied-profile rehearsal. It is an explicitly dirty build, not a tagged
release. Nothing has been pushed or published.

## Artifact and provenance

Directory: `build-appimage/dist/photos-2026-09-28/`

File: `Zuuned-0.1.0-g3a49a9506e3a-dirty-20260912T061036Z-x86_64.AppImage`

SHA256: `8bc90a5f29ab64fc078f843ef02ac91e56938159393a59ae9846485b690fea14`

Actual build time: **2026-09-28 18:09:35 UTC**. The filename's September 12
timestamp identifies the base commit date, not the build date. Build JSON,
environment report, pinned tool checksums, source provenance and `TESTING.txt`
are beside the executable. The copied executable's checksum was verified.

The exact pre-build source export is retained as `source-snapshot.tar.gz`:
`20690cab91a7e0f37348083dfc602577bb20cce3a542b222ba6d5f1b202e5688`.
This archive identifies the uncommitted changes; the base commit alone does not.

- App baseline: `3a49a9506e3acfc87c3a2904f76eab404a355623`, dirty.
- libzune: `d29bf7a5492b481f3211fbaeb3c596a00db7c742`, clean.
- Debian builder image: `77be80b0f064`; Debian 13, Qt 6.8.2, glibc 2.41.
- Source and build log: `/tmp/zuuned-photos-20260928/`.

## Verification

Native build, 29 photo DB checks, 73 photo service checks, 22 photo UI checks,
10 builder UI checks, adjacent playlist/music regressions and all 20 packaging
helper checks passed. The photo UI suites also passed X11/OpenGL and exercised
5,000-photo selection/builder cases. Photo service checks were repeated after
the shutdown fix and still passed.

Provider shutdown has six dedicated checks with stalled loopback HTTP requests
and a blocked MusicBrainz mutex waiter; observed drain was 2 ms. Existing provider
identity/cache suites passed another 72 checks. The fix cancels ArtworkHttp and
drains the global pool before QML services and QApplication are destroyed.
Independent TMDB/scanner/transcode cancellation is unchanged.

The Debian AppImage build and complete bundle inventory passed, including the
334 ELF dependency inventory, Qt/QML/portal, media tools, TLS/xcb and USB rules.
The complete desktop/photo/copied-profile rehearsal **passed September 28**:

- Fresh onboarding, native FLAC import with disc tags, Original/Halftone artwork,
  all three baseline desktop portal folder/open/save paths, playlist import,
  private logs, Health report export and normal shutdown.
- Actual packaged Photos folder import, staged parent/child creation through
  the floating builder, Save persistence and reopening after restart. Screenshots
  were inspected. This gate creates empty albums; membership/large selection
  and dragging retain their separate native/QML coverage above.
- A copied real v5 profile migrated to v7, preserving the full rollback backup,
  settings, 846 tracks, 7,751 videos, 80 photos, 7 playlists, 270 playlist-member
  rows and 7 collection customizations. The app closed normally within the
  harness's ten-second deadline, with the session log retained after teardown.

Evidence: `/tmp/zuuned-photos-20260928/runtime/runtime-DOHV6cuR/`;
`PASS.txt`, `photo-builder-PASS.txt` and `upgrade-preservation.txt` all report pass.
The tested candidate checksum matches the artifact above. The runtime had no
network or USB access, used disposable XDG profiles and software OpenGL, and
mounted only a read-only copy of the old profile. The live app was untouched.

Runtime image:
`sha256:fcf25b96a8b8579971bf326f168192ec1f7d5de1e309c58811406e40d981c214`.
Harness SHA256:
`d1a5a27e80bbc51d2405f7ae48e5b16ea48f91ea40d4c39c0f3b60aa0aae766f`.
The external harness is newer than the candidate source archive: its final
photo-folder click coordinates were corrected from an actual screenshot.
The earlier failed coordinate attempt is retained at
`/tmp/zuuned-photos-20260928/runtime/runtime-Rqh7oyT9/`; candidate bytes did not
change between those two attempts. A harness copy and gate summaries are beside
the AppImage, so its verified test inputs are retained separately.

## Testing scope

Follow `TESTING.txt` beside the AppImage. Physical photo transfer, clean eject
and device-screen presentation remain hands-on gates. Local nested albums do
not promise nested device folders. Host graphics/audio/USB and older distro
compatibility are separate from the isolated Debian desktop rehearsal.

Disc-aware duplicate detection is the next implementation phase; its
[audit](../audits/DISC_IDENTITY_2026-09-27.md) is complete. Expanded device editing
and hardware encoding remain later phases. Device-browser playback is excluded;
classic per-media storage discovery remains deferred.
