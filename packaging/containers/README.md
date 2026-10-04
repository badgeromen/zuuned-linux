# Debian build and clean runtime gates

These containers prepare and inspect the actual tester AppImage. Neither receives
USB devices, the developer's library, desktop sockets, or the running app's data.
The runtime image deliberately has no system Qt, mpv or FFmpeg packages. It does
provide the desktop services that real installed applications use:
`xdg-desktop-portal` and its GTK backend, GLib tools, desktop schemas and icons.
The candidate supplies its own Qt portal platform-theme plugin.

Both Dockerfiles pin this exact Debian 13 slim base:

```text
debian:13-slim@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132
```

The observed builder uses Qt 6.8.2 and glibc 2.41. The base digest does not pin
Debian package repository contents: preserve the built image IDs, package
versions and deployment-tool cache with release evidence. Reproducible output
byte for byte has not been established. A container pass does not establish
compatibility with older glibc distributions or physical graphics/audio devices.

## Build images

Run these commands from the repository root on the x86_64 development machine.
The examples use host UID/GID `1000:1000`, which must own the writable snapshot,
tool cache and evidence directories. The runtime Dockerfile creates a `tester`
account with that UID so D-Bus can resolve it. If another UID is required, change
the image account and mount ownership together; passing an arbitrary numeric
`--user` alone can fail before the application starts.

The Docker build context is **`packaging/containers`**, not the whole repository.
The runtime Dockerfile copies `smoke-runtime.sh` from that directory. Rebuild its
image whenever the smoke script changes.
Record the runtime harness revision separately when it differs from the
candidate's committed source. The runtime evidence also hashes the exact copied
smoke script, while the candidate's build JSON identifies the application.

```bash
set -euo pipefail
zuuned_repo="$PWD"
zuuned_evidence=$(mktemp -d /tmp/zuuned-release-XXXXXXXX)

docker build --platform linux/amd64 \
  --iidfile "$zuuned_evidence/builder-image.id" \
  -f packaging/containers/Dockerfile.debian13 \
  -t zuuned-tester-debian13 packaging/containers \
  2>&1 | tee "$zuuned_evidence/builder-image.log"

docker build --platform linux/amd64 \
  --iidfile "$zuuned_evidence/runtime-image.id" \
  -f packaging/containers/Dockerfile.runtime-debian13 \
  -t zuuned-runtime-debian13 packaging/containers \
  2>&1 | tee "$zuuned_evidence/runtime-image.log"
```

The `.id` files record the actual locally built images. Run those IDs below so a
later rebuild of a mutable image tag cannot silently change a gate's environment.
Image creation needs network access to install Debian packages. Runtime testing
uses `--network none`.

## Export and package a candidate

**Layout requirement:** `snapshot.mjs` currently requires a libzune Git
submodule. The public `badgeromen/zuuned-linux` snapshot vendors libzune as
ordinary files and is rejected by this exporter. The steps below describe the
private maintainer checkout. Public-clone packaging needs an exporter/provenance
update; native public-source compilation is documented in [BUILDING.md](../../BUILDING.md).

Finish and commit the release source, including any libzune pin change, before
the final export. Initialize the pinned submodule first if this is a new clone.
For each component observed clean, `snapshot.mjs` runs `git archive` against its
captured full commit ID, streaming a temporary tar file to disk. Later working
edits cannot replace those committed bytes in a supposedly clean snapshot.

An explicitly dirty component instead copies its tracked and non-ignored
untracked working files, retaining edits/deletions and a dirty label. Pause
editing for a consistent development rehearsal. A submodule HEAD different from
the captured app's gitlink marks the app snapshot dirty; the recorded libzune
revision identifies the actual included submodule commit. Symlinks and executable
modes survive both paths, without `.git` credentials or ignored build/tool-cache
files. The output must be a new directory outside the source tree. Provenance is
written only after both exports finish; failed partial exports retain no generated
`snapshot.env`. Do not rewrite its dirty flags to disguise a development snapshot.

```bash
test -z "$(git status --porcelain --untracked-files=normal --ignore-submodules=none)"
test -z "$(git -C libzune status --porcelain --untracked-files=normal)"
git submodule status libzune

zuuned_snapshot="$zuuned_evidence/source"
node packaging/containers/snapshot.mjs "$zuuned_snapshot"
grep -qx 'export ZUUNED_SOURCE_DIRTY=false' "$zuuned_snapshot/snapshot.env"
grep -qx 'export ZUUNED_LIBZUNE_DIRTY=false' "$zuuned_snapshot/snapshot.env"
cp "$zuuned_snapshot/snapshot.env" "$zuuned_evidence/source-provenance.env"

mkdir -p packaging/appimage-tools
zuuned_tools=$(realpath packaging/appimage-tools)
docker run --rm --platform linux/amd64 --user 1000:1000 \
  --env LANG=C.UTF-8 --env ZUUNED_APPIMAGE_TOOLS=/tools \
  --mount "type=bind,src=$zuuned_snapshot,dst=/work" \
  --mount "type=bind,src=$zuuned_tools,dst=/tools" \
  --workdir /work "$(cat "$zuuned_evidence/builder-image.id")" \
  bash -lc 'source /work/snapshot.env
            bash packaging/build-appimage.sh' \
  2>&1 | tee "$zuuned_evidence/candidate-build.log"
```

The build container can download missing deployment tools. The lock file checks
cached and downloaded bytes before execution, including the AppImage launcher
runtime. Preserve those exact tools; upstream `continuous` URLs can change.
This run does not pass through USB and does not launch the normal application.

For an explicitly labeled development rehearsal, an uncommitted snapshot may
be built with `--env ZUUNED_ALLOW_DIRTY=1`; omit the two clean-state assertions
for that rehearsal. It is not the final release artifact. After fixes are
committed, export a new snapshot and rebuild without that override.

Outputs are in `$zuuned_snapshot/build-appimage/dist/`: the versioned AppImage,
SHA256, build JSON, tool lock and environment report. Packaging runs
`verify-appdir.sh`, including the recursive static ELF dependency inventory.
That inventory reads every bundled ELF/plugin and permits external SONAMEs only
from [appimage-host-libraries.txt](../appimage-host-libraries.txt). It does not
use development-host `ldd` success as proof that a library was bundled.

## Run the actual AppImage in the clean desktop

Select the exact output filename; never substitute the old tester AppImage.
The candidate must be executable. The only host mounts are that read-only
candidate and a writable evidence directory. No USB, GPU device, user profile,
X11 socket, audio socket or source tree is mounted.

```bash
zuuned_candidate=$(realpath "$zuuned_snapshot/build-appimage/dist/Zuuned-<exact-filename>.AppImage")
zuuned_runtime_output="$zuuned_evidence/runtime"
mkdir "$zuuned_runtime_output"

docker run --rm --platform linux/amd64 --user 1000:1000 \
  --network none --shm-size 512m --mount type=tmpfs,dst=/sys/bus/usb/devices \
  --mount "type=bind,src=$zuuned_candidate,dst=/candidate.AppImage,readonly" \
  --mount "type=bind,src=$zuuned_runtime_output,dst=/evidence" \
  "$(cat "$zuuned_evidence/runtime-image.id")" \
  /candidate.AppImage /evidence \
  2>&1 | tee "$zuuned_evidence/runtime-console.log"
```

The script rejects an exposed `/dev/bus/usb`, a Zune visible through sysfs, any
network interface other than loopback, or installed system Qt/mpv/FFmpeg packages.
The empty USB sysfs mount prevents host hotplug from changing the test viewport. It uses Xvfb at 1440×1000,
Mesa software OpenGL, a private D-Bus/Openbox session, null audio and disposable
XDG paths. It starts real desktop portal and GTK backend processes, selecting
GTK only in this disposable desktop's `portals.conf`. Neither the app nor the
test changes the host's desktop preference. The app selects its portal theme
through its own AppRun; the harness does not force Qt's picker. It gracefully
closes only the windows it launched; failure cleanup targets its own process IDs.

Each run creates `/evidence/runtime-XXXXXXXX`; `latest-runtime.txt` identifies
that container-relative path. Logs and partial evidence survive failures. A
loader failure leaves reports for all three bundled executables before any app
launch is attempted. `PASS.txt` is written only after every scripted assertion
passes; this README does not mark any candidate's runtime gate passed.
The application's own automatic session logs are copied into `local-logs/`
while running, after normal shutdown, and on failure. Each snapshot has original
file permissions, sizes and SHA-256 hashes in `inventory.txt`. The fixture and
optional upgrade have separate disposable `XDG_STATE_HOME` directories; no
developer session logs are read or collected.

The gate checks:

- The shipped AppImage's no-FUSE `--version` route and extracted `AppRun` agree.
  Each runs with an empty HOME and XDG profile; neither may create settings,
  library, cache, state, runtime or log files there.
- The bundled FFmpeg tools generate and decode short FLAC/WMV fixtures and JPEG
  art, and capture the actual application's desktop frames.
- Fresh onboarding loads and its real Skip action reveals empty Appearance.
- Settings → Library → browse opens the actual desktop folder chooser. Selecting
  the fixture directory adds the watch folder through the production UI and
  imports tagged FLAC with correct album/track/disc metadata. The harness no
  longer inserts a watch-folder database row.
- Original Appearance loads; a Halftone preference written to the disposable
  QSettings file while stopped survives restart and produces native print-cache
  output. Settings, source identity, screenshots, logs and SQLite integrity
  results are retained.
- Every GUI launch creates a new automatic session log under its isolated state
  directory, with the session header and existing native `fprintf(stderr)`
  startup/build line. Logs must remain readable after normal app shutdown, with
  directory mode `0700`, file mode `0600`, and observed limits of eight files and
  2 MiB per file. This launch gate does not force high-volume rotation or inject
  credentials; the logger's separate regressions cover those behaviors.
- Settings → Mixtapes → import opens the actual desktop file chooser. Selecting
  a generated `.m3u` imports its playlist and the expected track membership.
- Health's actual export button and desktop SaveFile dialog save a debug report through
  normal mouse/keyboard input. The gate requires its title, system/log sections,
  both full candidate revisions and native startup logs, mode `0600`, and a
  bounded file size. The report, hash and chooser/result captures stay with the
  evidence. Only `diagnostics-export.txt` reporting `PASS` establishes that this
  UI export succeeded; observing local log files alone does not.

`portal-picker-checks.txt` must report all three desktop picker paths passing.
For each path, the gate checks a public FileChooser request from the candidate's
D-Bus/PID, forwarding by the actual portal process, and a visible dialog owned
by the GTK backend. The request's response must match its handle, report success,
and return the exact selected fixture URI. Folder requests also require
`directory=true`; ordinary file requests must not set it. A Qt fallback dialog
cannot satisfy these checks. Raw D-Bus messages, isolated portal configuration,
daemon logs, dialog captures, and per-request/response records remain in the
evidence. This follows the [FileChooser interface](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html)
and [desktop backend selection](https://flatpak.github.io/xdg-desktop-portal/docs/portals.conf.html).

## Optional packaged photo organizer

Add `--env ZUUNED_PHOTO_GATE=1` to the runtime Docker command to exercise the
photo organizer in the disposable profile. This imports the generated JPEG
through the actual Settings photo-folder chooser, then uses the Photos builder
UI to stage and save an empty parent album and a nested child. Read-only SQL
checks verify that draft names are absent before Save, the child's parent is
correct, and both records survive a normal restart. Captures retain the builder
and reopened parent view. No database rows or application test hooks seed this
workflow.

An enabled photo gate fails the runtime run on any failed assertion.
Only `photo-builder-PASS.txt` certifies these photo checks; the ordinary runtime
`PASS.txt` alone does not. The additional photo-folder portal request is verified
separately from the three baseline folder/open/save requests. This small packaged
gate does not certify photo album membership, bulk selection, dragging or device
transfers; their separate software/hardware gates still apply. Mouse positions
assume the fixed 1400×920 candidate window and must be checked against the saved
captures when layouts change.

## Optional copied-profile upgrade

The same runtime gate can additionally launch a **copy** of a v5 library with
its settings. Prepare a SQLite-consistent `library.db` backup (for example with
SQLite's `.backup`, not a bare copy of a live WAL database) and `Zuuned.conf` in
a separate directory. Do not pass the live profile, media folders or cache.
The test copies those inputs into another fresh XDG tree before opening them.

```bash
zuuned_upgrade_input=$(realpath /absolute/path/to/prepared-v5-backup)
docker run --rm --platform linux/amd64 --user 1000:1000 \
  --network none --shm-size 512m --mount type=tmpfs,dst=/sys/bus/usb/devices \
  --env ZUUNED_UPGRADE_INPUT=/upgrade-input \
  --mount "type=bind,src=$zuuned_candidate,dst=/candidate.AppImage,readonly" \
  --mount "type=bind,src=$zuuned_upgrade_input,dst=/upgrade-input,readonly" \
  --mount "type=bind,src=$zuuned_runtime_output,dst=/evidence" \
  "$(cat "$zuuned_evidence/runtime-image.id")" \
  /candidate.AppImage /evidence \
  2>&1 | tee "$zuuned_evidence/upgrade-runtime-console.log"
```

This optional pass requires v5→v8 migration, SQLite integrity, the application's
v5 rollback backup with an identical SQL dump to the input, and preservation of
the old tables' content except named provider retry counters/timestamps. It also
checks existing copied settings and records another actual-app capture. Only
`upgrade-preservation.txt` reporting `PASS` establishes that this particular
copied profile passed. The ordinary generated-fixture pass alone says nothing
about an existing profile. The input remains read-only and the actual watched
media stays outside the container. For this copied-profile launch only, warnings
about absent local-file QQuickImage sources (including `file://~/` after home
path redaction) are retained in the application console log but
excluded from the QML-error gate; old media/cache paths are intentionally absent.
Other QML errors still fail the gate.

## Scope and local helper tests

Review the screenshots as well as the exit result. The picker gate certifies the
isolated X11/GTK portal desktop; KDE, GNOME, Hyprland and other installed desktop
backends still need their own host check. It does not test clicking the Halftone
control or mpv playback through real audio output, or certify live providers/TLS connectivity,
profiles that were not supplied to the optional gate, a physical GPU, the host desktop session, USB permissions,
hotplug or media transfers. ALSA/PipeWire backend modules and configuration also
need a host playback rehearsal. Record those separate gates and the exact
candidate checksum in [TESTER_RELEASE.md](../../docs/TESTER_RELEASE.md).

`bash packaging/tests/run.sh` runs the separate metadata/AppRun, real-ELF
inventory and isolated Git-export regressions without Docker or the application.
The export cases include worktree changes after clean-state capture, committed
media exceeding subprocess output-buffer size, dirty additions/deletions,
submodule pin mismatches, protected destinations and failed-archive cleanup.
