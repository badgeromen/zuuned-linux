# Photo album testing candidate — September 27, 2026

Superseded by the verified [September 28 candidate](TESTER_PHOTOS_2026-09-28.md).

Built September 27 at 17:21 UTC from an explicitly dirty development snapshot.
This candidate contains custom/nested photo albums, the floating photo builder,
offline local photo drag/drop, bulk selection, and staged membership/order edits.
It is not a tagged release. No changes have been pushed.

## Artifact

Directory: `build-appimage/dist/photos-2026-09-27/`

File: `Zuuned-0.1.0-g3a49a9506e3a-dirty-20260912T061036Z-x86_64.AppImage`

SHA256:
`40b9e329939ba7eec6df0df001ced54860c90d0d9a5cc98c9d1fee86052b118c`

The filename timestamp is the base commit's timestamp, not the build date.
The directory also contains the checksum, build JSON, environment/tool records,
source provenance, and `TESTING.txt` with a hands-on checklist.

Source baseline: `3a49a9506e3acfc87c3a2904f76eab404a355623` plus the uncommitted
photo changes. libzune: `d29bf7a5492b481f3211fbaeb3c596a00db7c742` (clean).
Frozen source/build log: `/tmp/zuuned-photos-20260927/`.
Builder image: `77be80b0f064`, Debian 13, Qt 6.8.2, glibc 2.41.

## Verification

- Native build and focused photo gates passed: 29 DB, 73 service, 22 photo UI,
  and 10 builder UI checks, including 5,000-photo cases. Photo/builder suites
  passed offscreen and X11/OpenGL. Adjacent playlist/music gates passed.
- All 20 packaging/export regressions passed.
- Debian AppImage build and bundle inventory passed: 334 ELF files, native
  Qt/QML, portal plugin, media libraries/tools, TLS, xcb and canonical USB rule.
- Copied artifact checksum verified against the built candidate.
- The first isolated runtime attempt stopped because obsolete mouse coordinates
  missed the Library tab; no portal request occurred. Evidence is retained at
  `/tmp/zuuned-photos-20260927/runtime/runtime-xp2kCEaD/`. Only the external
  harness coordinates changed for the rerun; the candidate bytes are unchanged.

The corrected harness passed fresh-profile import, Original/Halftone rendering,
all three real portal picker paths, and Health report export. Its copied-profile
run then failed the normal-shutdown deadline with background artwork requests
still running (`QEventLoop: Cannot be used without QApplication`). Evidence:
`/tmp/zuuned-photos-20260927/runtime/runtime-prd9nQDM/`.
This first candidate has **not passed the complete runtime/upgrade gate** and
must be replaced by a verified build with the provider-shutdown fix. No hardware
or live profile was exposed to either container attempt.

## Hands-on scope

Use `TESTING.txt` beside the AppImage. Exercise create/nest, Ctrl/Shift/Select All,
drag/drop, Save/Cancel, reorder, rename/move and delete-with-source-preservation.
The v7 migration uses the existing profile backup mechanism.

Physical photo transfer, clean eject and presentation on the Zune remain manual
gates. Local nesting does not promise nested device folders. Host audio/GPU/USB
and older distro compatibility are not certified by the container rehearsal.

Disc-aware dedup is audited but not implemented in this candidate; see
[the audit](../audits/DISC_IDENTITY_2026-09-27.md). Expanded device editing and
hardware encoding remain later phases. Device-browser playback is out of scope;
classic per-media storage discovery remains deferred.
