# Tester release preparation

The recorded `tester-r2` desktop-dialog candidate is built and passes the clean
runtime, native picker and report-export rehearsal. Its exact artifact and
evidence are in [TESTER_R2_DESKTOP_DIALOGS.md](releases/TESTER_R2_DESKTOP_DIALOGS.md). It has not been
publicly published; the remaining hands-on checks below are explicit.

**September 28 photo testing build:** a new development AppImage includes
custom/nested photo albums, the floating builder and the provider-shutdown fix.
Packaging, real desktop dialogs, packaged parent/child album creation and the
copied v5→v7 profile upgrade/shutdown gate passed. Exact artifact, checksum and
evidence: [TESTER_PHOTOS_2026-09-28.md](releases/TESTER_PHOTOS_2026-09-28.md).
Physical photo-transfer verification remains separate. See `PHOTO_ALBUMS.md`
and `FEATURE_COMPLETION.md` for scope. Device-browser video playback is out of
scope; classic per-media storage discovery is deferred.

## What is already known

- Tester-preparation software checks: native app build; 33 real-media import
  checks; 64 native and 64 fallback metadata-probe checks with sanitizers;
  19 playlist/schema checks; 23 music/QML checks; 20 packaging/export checks;
  14 isolated USB-installer checks plus `udevadm verify`. The USB fixtures
  replace `udevadm` with a recorder and never touch a device or system rules.
  These results do not substitute for the artifact/device gates below.
- Another 230 native regressions passed across Customize, playback, playlist
  services, music identity/provider caching and video title/repair paths.
- The current candidate includes automatic local logs and Health debug-report
  export: 72 native core checks (also ASan/UBSan), 34 report/QML checks and the
  native app build pass. The exact `87b2ecd` AppImage passes actual desktop
  folder/open/save dialogs, private session-log lifecycle and fresh/copied-profile
  runtime checks; see [DIAGNOSTICS.md](DIAGNOSTICS.md) and
  [DESKTOP_FILE_DIALOGS.md](DESKTOP_FILE_DIALOGS.md).

- The September 2 fresh-HOME AppImage rehearsal and transfers are documented in
  [FRESH_RUN_LOG.md](FRESH_RUN_LOG.md). That was a prepared development machine,
  not a clean distro image; generic libmtp rules already granted USB access.
- The September 8 source has native regression coverage for Customize, playback,
  queue actions, title metadata and local artwork rendering. See the relevant
  test READMEs and [ARTWORK_STYLE_STUDY.md](ARTWORK_STYLE_STUDY.md).
- The current development toolchain is Arch x86_64 / Qt 6.11.2. The declared Qt
  floor is 6.8 (QTP0004 and Image.retainWhileLoading). A successful development
  build does not establish binary compatibility with another distro.

## Candidate inputs and output

Build the reviewed, committed source with its exact libzune pin:

```bash
bash packaging/build-appimage.sh
```

The builder refuses a dirty checkout by default. `ZUUNED_ALLOW_DIRTY=1` permits
a clearly labeled development artifact; do not advertise it as a reproducible
tagged release. The builder records app/libzune hashes and dirty state in About,
`--version`, startup logs, and `usr/share/zuuned/zuuned-build.json`.

For a Git-free source snapshot, pass full 40-character
`ZUUNED_SOURCE_REVISION`/`ZUUNED_LIBZUNE_REVISION`, explicit
`ZUUNED_SOURCE_DIRTY`/`ZUUNED_LIBZUNE_DIRTY` (`true` or `false`), and
`SOURCE_DATE_EPOCH`. CMake accepts the same names with `-D`. Unknown provenance
stays `unknown`/JSON `null`; absence of Git metadata never means clean. Only set
`false` when the snapshot actually comes from those committed trees.

Outputs go to `build-appimage/dist/`: versioned AppImage, SHA256, build JSON,
tool lock and build-environment report. `SOURCE_DATE_EPOCH` defaults to the app
commit time and determines the UTC timestamp in the filename. The environment
report includes GLIBC symbol requirements from all bundled ELF files; that lower
bound alone is not a successful distro test.

`packaging/appimage-tools.lock` checksum-pins the cached tools used for the
earlier tester build. Both cached and downloaded binaries are checked before
execution. The upstream `continuous` URLs are mutable: preserve the pinned tool
files with build inputs, or review and commit a deliberate lock update when
those bytes are unavailable. `ZUUNED_APPIMAGE_TOOLS` selects an existing tool
cache. These pins improve input traceability; reproducible output also requires
a pinned build image and dependency versions. Bit-for-bit reproducibility has
not been established.

The launcher runtime is checksum-pinned too; `LDAI_RUNTIME_FILE` prevents
appimagetool from downloading a different runtime during packaging. See
[the container recipe](../packaging/containers/README.md) for committed-tree
source export, the pinned Debian baseline and the isolated desktop rehearsal.

The bundle inventory gate checks GraphicalEffects and other QML modules, Qt,
mpv/libav/LAME/SQLite/USB/crypto libraries, xcb/JPEG/TLS plugins, `ffmpeg` and
`ffprobe`, the canonical USB rule and build metadata. The custom AppRun prepends
the bundled tools to PATH: libzune still invokes FFmpeg for photo preprocessing
and a thumbnail fallback. Inventory checks do not prove runtime compatibility.
Every bundled ELF, including Qt/QML plugins, also has its transitive dependency
names checked against the bundle and a narrow host graphics/glibc allowlist.
Audio, USB, crypto and C++ runtime libraries cannot silently depend on the
builder's installed packages. Qt's dynamically loaded OpenSSL libraries have
explicit inventory checks.

The Debian 13 development-candidate rehearsal passed on September 8 after two
packaging corrections: dependencies excluded by linuxdeploy now ship, and QML
resources live at the module root to avoid Qt 6.8.2's ambiguous subdirectory
import. The native Qt 6.11.2 build also passes with that resource layout.
Runtime evidence is `/tmp/zuuned-r2-runtime-evidence/runtime-p2Ti64ne/`: actual
AppImage identification, onboarding/Skip, bundled media tools, FLAC import,
Original/Halftone Appearance, persisted settings and normal shutdown. A copied
real v5 profile migrated to v6 with all 846 tracks, 7,751 videos, 80 photos,
7 playlists, 270 membership rows and 7 collection customizations preserved.
The full v5 rollback backup matches its input. Original media/cache paths remain
in that copy but their files are intentionally unmounted; the corresponding
local-image warnings are retained and excluded only from the upgrade check.
This is development-candidate evidence; the final clean artifact must repeat
the runtime gate and record its own checksum below.

## Gates before announcing the candidate

| Gate | Evidence to record | Status |
| --- | --- | --- |
| Exact candidate | Clean app/libzune revisions and SHA256 in candidate record; copied artifact verified | Passed |
| Clean source build/install | Fresh committed-tree export built/installed in pinned Debian 13 baseline | Passed |
| Bundle inventory | 334 ELF files, including the Qt desktop-portal plugin; recursive dependency gate and media tools | Passed |
| Clean runtime | Actual final AppImage, Original/Halftone rendering, media tools and normal shutdown | Passed for scripted scope; physical playback remains |
| Fresh profile | Actual onboarding/Skip, native desktop folder selection, FLAC import, persisted artwork setting | Passed for scripted scope; live-provider/manual fallback rehearsal remains |
| Existing-profile upgrade | Copied v5 database/settings preserved with exact rollback backup; real host migration also creates backup | Passed for supplied profile |
| Native desktop pickers | Exact `87b2ecd` AppImage; real GTK portal directory/file/save requests, backend-owned dialogs and selected-URI correlation | Passed on isolated X11/GTK portal; Solus desktop retry remains |
| Tester diagnostics | Exact `87b2ecd` AppImage; real Health/desktop SaveFile export, candidate IDs, bounded private logs and session shutdown; side-effect-free version output | Passed |
| USB rule | 14 installer fixtures and syntax verification; stock-rule ACL regeneration and native app connection pass on Omarchy without custom Zune rules ([audit](audits/USB_ACCESS_2026-09-09.md)) | Access-denied installer recovery and physical hotplug rehearsal remain |
| Host hardware | Draco 120 GB: MP3 readback, ordered playlist, 20/20 sends, video metadata, verified cleanup/finalization | Passed for CLI scope; app photo sync and physical playback remain |

Debian 13/Trixie is the proposed compatibility build baseline: system Qt 6.8.2
and glibc 2.41. Build/install and no-USB runtime checks may run in a container.
Container success does not certify desktop GPU/audio integration. Perform the
graphics/session checks on a desktop and all USB operations on the host, through
the serialized app/device tooling; do not pass USB into the build container.
Ubuntu 22.04/24.04, Mint, native Wayland and non-systemd setups are not currently
certified targets. Expand the release matrix only after an actual gate passes.

## USB rule reasoning

`68-zuuned.rules` runs before libmtp's `69-` probe. A second copy named
`72-zuuned.rules` reapplies the same exclusions after generic MTP rules. Both run
before systemd's `73-seat-late` ACL assignment. They match only Zune USB device
nodes, add `uaccess`, and clear MTP/gphoto classification. Actual `udevadm verify`
showed that ENV fields do not support final `:=` assignment; the two-stage
ordering avoids relying on that invalid syntax. The old late `99-` rule could
miss ACL setup without generic libmtp tagging; the old `45-` rule allowed later
overrides. Both installed files and the in-app installer use one canonical
resource. Syntax verification alone does not certify actual hotplug/ACL behavior.

## Publishing and deferred work

Publish the exact checksum, tested distro/device matrix and known limitations
with [TESTER_INSTALL.md](../packaging/TESTER_INSTALL.md). Keep every report tied
to the filename and About build ID. The Arch `-git` package intentionally follows
the default branch; it is not an immutable candidate.

Before announcing, finish a short hands-on music/video playback and photo-sync
pass, plus the first-install picker/USB setup on a tester's desktop. Virtual
photo albums/subalbums are in newer source (`PHOTO_ALBUMS.md`). Device deduplication still does not
distinguish otherwise identical titles across discs; local disc ordering does
not change that device identity contract.

**USB-card layout follow-up (September 8):** the permission card now constrains
its text to the available width and places the right-aligned "Fix it for me"
button on its own line. The selectable manual command scrolls inside the card.
The native build and rendered 200/236 px cards, including the expanded command,
were checked without invoking the installer. This source fix follows the
recorded `6dbf632` r2 AppImage; that existing artifact does not contain it.

**Direct-launch identity and device music imports (September 9):** the Qt
process now declares the `zuuned` desktop-file identity and carries a compiled
256 px application icon, so a directly opened AppImage can present the ZUUNED
mark in the live window/taskbar. Successful device album/artist/track pulls now
register and scan the music import root once per completed batch, then prime the
existing cached artist matcher for each distinct artist. The application-
identity and device-music-import native gates exercise the resource and batch
logic without opening USB.

**Solus 4.9 field report (September 9):** the exported report from a 1920x1080
Budgie/X11 host proves that the AppImage launches with its bundled Qt 6.8.2.
Earlier sessions in the same bounded log authenticated and read both a Pavo
(Zune HD) and a Keel (Zune 30) without an access-denied result. The latest
`762c304` session launched and edited MP3 tags successfully, but no Zune was
present during that session, so its new post-import scan/matcher path still
needs a device retest. The report exposed two import defects now covered by
native/QML gates: artist drill-down tracks are grouped by album and ordered by
disc/track instead of title, and same-title device objects receive collision-
safe `(2)`, `(3)` filenames rather than overwriting an existing or queued
extract. MusicBrainz returned real HTTP 503 responses and Deezer repeatedly
closed connections during the run; those provider failures are distinct from
the successful USB and local-import paths.

**Device album-art preservation (September 9):** a successful device music
pull now follows the track-to-album-object mapping and fetches the album's
representative-sample JPEG from the Zune. The image is validated, bounded to
the local 400 px cache contract, and installed under the same album-artist and
album key used by the scanner. It never replaces an existing local or custom
cover. ZMDB artist records expose only artist ID and name, so there is no
device-side portrait to import; artist portraits continue through the cached
online/custom pipeline and its flat fallback. Native gates cover album-owner
identity, request deduplication, image normalization and no-overwrite behavior.

Flatpak, .deb/.rpm, automated CI and more device/distro models can follow the
scoped tester release. They do not replace the gates above.
