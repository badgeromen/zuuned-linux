# Tester r2 desktop-dialog candidate — September 9, 2026

This local candidate restores the desktop's file and folder picker. The
[diagnostics candidate](TESTER_R2_DIAGNOSTICS.md) accidentally omitted the Qt
portal platform plugin and could show Qt's internal fallback browser. The root
cause and desktop integration contract are in
[DESKTOP_FILE_DIALOGS.md](../DESKTOP_FILE_DIALOGS.md).

## Exact artifact

- Application: `87b2ecdfd289ae11e18d5dc235a5811180243a8e`, clean.
- libzune: `d29bf7a5492b481f3211fbaeb3c596a00db7c742`, clean and unchanged.
- File: `build-appimage/dist/Zuuned-0.1.0-g87b2ecdfd289-20260909T162612Z-x86_64.AppImage`.
- SHA256: `a0d2102194d9d547cce6e15ee266f0fe9c2f2e73e54c5a74b4365e0caff02c12`.
- Matching build JSON, checksum, environment and tool lock are alongside it.

The app/submodule were exported from a clean detached checkout of these exact
commits. Runtime harness changes are recorded separately from the shipped
application revision. No dirty development snapshot is used in the artifact.

## Verified build

The native build and 20 packaging-helper checks pass. The strengthened inventory
rejects the previous AppDir specifically for its missing portal plugin.

Builder image:
`sha256:77be80b0f0649b23db7eb293cf977c16c098e53265e7e5baab45c7d8d17bd759`.
The candidate built on Debian 13 with Qt 6.8.2. All 334 bundled ELF files pass
recursive dependency verification, including the newly bundled
`platformthemes/libqxdgdesktopportal.so`. Its copied AppImage checksum matches.

## Runtime verification

The expanded gate requires actual desktop portal folder/open/save requests,
backend-owned chooser windows, and successful responses containing the selected
fixture paths. It also retains the diagnostics, fresh profile and copied v5
upgrade checks. A fallback picker cannot satisfy the portal evidence checks.

The first attempt retained evidence in
`/tmp/zuuned-native-dialog-runtime-evidence/runtime-MwrXH7sn/`. It did not reach
the picker: restarting an empty library showed onboarding again. The harness
now keeps the session where Skip was clicked and navigates to Library before
adding its first folder. Further retained attempts corrected the browse position
and scrolled to the visible playlist-import control. These are harness
corrections; they do not modify the candidate or seed watch folders in SQLite.

**Final pass:** `/tmp/zuuned-native-dialog-runtime-evidence/runtime-Rx2k7eYD/`.
The executed harness is commit `e5cf588d51520c927f8194878d2324c77228e765`,
SHA256 `7a36e5308ca9304d4f74ba70db76f2c190857543bfd97b116170e82e74b6fbba`.
Its bytes match the hash recorded by the actual runtime invocation.

- Directory OpenFile selected the fixture folder through the GTK desktop
  chooser; the app then imported its FLAC with correct disc/track metadata.
- Ordinary OpenFile selected a playlist; the app imported the expected track
  membership from that file.
- SaveFile produced a private debug report containing the exact candidate
  revisions and native session logs. Its SHA256 is
  `5293db102595eb2f5fb44277aeedfb4cc2fd768a9590b8cbeed57165d5e85ae0`.
- All three paths correlate the candidate's D-Bus sender/PID, public portal
  forwarding, GTK-owned visible dialog and successful response with the exact
  selected URI. The dialog captures were visually reviewed.
- Identification/extraction, bundled media tools, Original/Halftone rendering,
  persisted settings, native log lifecycle and normal shutdown pass.
- A copied v5 profile migrated to v6 with all 846 tracks, 7,751 videos,
  80 photos, seven playlists, 270 membership rows and seven collection overrides
  preserved, plus an exact v5 rollback backup and unchanged existing settings.

Curated local evidence is in `build-appimage/evidence/tester-r2-87b2ecd/`.

Desktop runtime image:
`sha256:9d403b18ba5ebca9ded6f5f7c338c4de3c93d37847aafa4eb5dd0761203165c2`.
It provides real GTK portal services, with no system Qt/mpv/FFmpeg, USB device
nodes, host desktop sockets or network access. The harness may be mounted
read-only for iteration; its exact executed bytes are hashed in the evidence.
The container's GTK backend does not certify the tester's Solus desktop backend.
The document portal logs its expected unavailable FUSE mount in this isolated
container; the non-sandboxed app's FileChooser requests all succeed. This is not
a test of sandboxed document export. No host Zune was enumerated during the
final run; documented future invocations mask USB sysfs to keep host hotplug
from changing the test viewport.

The exact AppImage also opens on the Omarchy development desktop with bundled
Qt 6.8.2, an initialized OpenGL context and no QML warnings. Its process maps
confirm it loaded the bundled `libqxdgdesktopportal.so`; the host exposes
FileChooser version 4. Host log: `/tmp/zuuned-desktop-dialog-candidate-host.log`.
The preceding app was closed normally after checking its current log, stopped
playback and absent transfer marker. No desktop theme/configuration was changed.

The prior candidate's unchanged-protocol hardware evidence remains applicable
within its documented scope. No transfers or firmware-playback checks are
claimed for this dialog-only change. No public release or Git push has occurred.
