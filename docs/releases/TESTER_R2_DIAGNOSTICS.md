# Tester r2 diagnostics candidate — September 9, 2026

This historical candidate is superseded by the
[desktop-dialog candidate](TESTER_R2_DESKTOP_DIALOGS.md). It added the USB
permission-card layout fix and automatic local diagnostics to the
[previous candidate](TESTER_R2.md). It has not been publicly published.

## Exact artifact

- App: `9a6eeb6b729628f245e04575544f4a032ef04206`, clean.
- libzune: `d29bf7a5492b481f3211fbaeb3c596a00db7c742`, clean and unchanged.
- File: `build-appimage/dist/Zuuned-0.1.0-g9a6eeb6b7296-20260909T152804Z-x86_64.AppImage`.
- SHA256: `d340e0746a6c77dbc6d5b1ff97b719a1eec1a7b302cf175c805416503410f546`.
- Matching checksum, build JSON, tool lock and environment report are alongside
  the AppImage. The copied artifact passes its checksum verification.
- Curated local evidence: `build-appimage/evidence/tester-r2-9a6eeb6/`.

Subsequent documentation commits record results without changing the artifact's
source revision. Preserve the complete filename/build ID in tester reports.

## Diagnostics and native checks

Settings → Health → **export debug report** saves a local text report with
system/build information and recent session logs. **open logs** exposes the
automatic bounded log directory; **open report** appears after a successful
save. Reports are reviewed and shared by the tester, never uploaded by the app.
See [DIAGNOSTICS.md](../DIAGNOSTICS.md) for retention and redaction limits.

The native app build, 72 logger/redaction checks (also passing ASan/UBSan),
34 report/service/QML checks and 20 packaging-helper checks pass. The native
Arch build opened on Health with Qt 6.11.2, created a private session log,
authenticated the attached Zune and emitted no QML warnings.

## Exact packaged runtime

A fresh export of the committed app/submodule built and installed using the
pinned Debian 13 baseline, Qt 6.8.2 and glibc 2.41. All 333 bundled ELF files
pass the recursive dependency inventory. Bundled symbol requirements reach at
most `GLIBC_2.39`; older distributions still require their own runtime test.

- Builder image: `sha256:44f0233fb34f0f7b1a4b72f9e95bdfa2e0494843e4262d22d9497e182d74d544`.
- Runtime image: `sha256:4b57862ff6248355a04b38ce494072ef58e1dbfd86bc485059250370aadf7214`.
- Runtime evidence: `/tmp/zuuned-diagnostics-runtime-evidence/runtime-nOEQOTcZ/`.

The actual AppImage passes identification/extraction, bundled media-tool
encode/decode, onboarding/Skip, FLAC import with disc metadata,
Original/Halftone rendering, saved settings, normal shutdown and DB integrity.
Both `--version` entry points leave their empty HOME/XDG profiles untouched.
Each GUI launch creates its own private native session log, with startup/build
and normal session-end markers; retained files stay within their limits.

The real Health button and SaveFile dialog saved an 8,961-byte, mode-0600 report
containing exact candidate revisions, system details and captured native logs.
The saved-report UI and chooser were captured and reviewed. The report hash is
`49ab12fe1d9db56ee9f38458dfef41919b6fc0fce8e23e8d77df43d1acbcc72a`.

A copied real v5 profile migrated to v6 with all 846 tracks, 7,751 videos,
80 photos, seven playlists, 270 membership rows and seven collection overrides
preserved. Its complete v5 rollback SQL dump equals the input, and existing
settings retain their values. Missing-image warnings refer only to intentionally
unmounted original files and are retained in the local evidence.

The container has no system Qt/mpv/FFmpeg, USB device nodes, network access,
host media or desktop sockets. It uses software OpenGL and null audio. Shared
read-only sysfs still identifies the host GPU and Zune hardware; those report
entries do not establish GPU use or device access inside the container.

The same AppImage opened on the Arch/Omarchy host through XWayland with bundled
Qt 6.8.2. Its OpenGL context initialized, Health opened without QML warnings,
the Zune authenticated and its inventory was read, and an owner-only native
session log was created. Before replacing the native app, its current log,
stopped playback and absent transfer marker were checked and its window was
closed normally. Host log: `/tmp/zuuned-diagnostics-candidate-host.log`.
No transfers were initiated for this diagnostics follow-up.

## Remaining hands-on checks

The unchanged libzune pin retains the previous candidate's Draco 120 GB CLI
rehearsal evidence: MP3 readback, ordered playlists, 20/20 repeated sends, video
metadata and verified cleanup. These tests were not repeated for logging.
Physical playback, packaged photo sync, access-denied installer recovery,
physical hotplug and the fresh watch-folder picker/provider workflow remain
explicit checks in [TESTER_RELEASE.md](../TESTER_RELEASE.md). This is a scoped
tester candidate, not a claim of support for every distro or device model.
