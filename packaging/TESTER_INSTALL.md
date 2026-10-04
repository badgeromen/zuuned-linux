# Zuuned — tester install

Use the exact candidate linked in the release announcement. The short tester
filename identifies the version and architecture; the matching build JSON and
`--version` output identify its exact app/libzune revisions.
See the [tester quick start and reporting guide](TESTER_README.md). The announcement
must list tested distributions and device models; the old September 2 AppImage
does not contain the later fixes. Current gate status: [TESTER_RELEASE.md](../docs/TESTER_RELEASE.md).

## AppImage

The candidate targets **x86_64, X11 or XWayland, and working OpenGL**. It includes
Qt/QML, mpv, codec libraries, FFmpeg tools and the protocol stack. It still uses
the host kernel, glibc, graphics/audio services and USB permissions. A newer-host
build may fail on an older distribution; check the candidate's compatibility
notes first. Native Wayland and non-systemd device setup are not yet certified.

File/folder pickers use the desktop's own chooser through `xdg-desktop-portal`,
with the desktop's theme, sidebar and mounted locations. The session needs a
working FileChooser portal backend (provided by the desktop, usually GTK or
KDE). The AppImage supplies its matching Qt integration plugin. A plain Qt
fallback picker means this integration is unavailable; include that symptom in
the debug report rather than treating it as ZUUNED's intended picker.

1. Download the versioned `.AppImage` and matching `.AppImage.sha256` into the
   same directory. Set `candidate` to the downloaded filename:

   ```bash
   candidate='./Zuuned-Linux-0.1.1-x86_64.AppImage'
   sha256sum --check "${candidate}.sha256"
   chmod +x "$candidate"
   "$candidate" --version
   "$candidate"
   ```

   `--version` prints the app/libzune build identification and exits before any
   GUI, library scan or device service starts.

2. If the AppImage runtime reports missing FUSE, use extraction mode:

   ```bash
   APPIMAGE_EXTRACT_AND_RUN=1 "$candidate"
   ```

3. Plug in the Zune. The USB-rule card offers **Fix it for me**, using a native
   administrator prompt, and a copyable terminal command if polkit is absent.
   This installs `68-zuuned.rules` and `72-zuuned.rules` from one canonical source
   and needs root once. It uses systemd-udev and an active desktop session.
   Replug if requested. An older `45-zune.rules` or `99-zune.rules` does not
   replace the current pair.

The canonical bytes are in the source release at `packaging/68-zuuned.rules`;
the installer writes that content under both filenames. No source checkout is
required for the in-app installer.

## Source / Arch package

Use the [README](../README.md#build) for the compiler, Qt 6.8+ and QML packages.
The public tree includes libzune as ordinary source. No submodule setup is
required. Follow [authentication setup](../docs/BUILD_WITH_MTPZ.md) for device
use. The migrated 0.1.1 executable predates the new public source history; a tag
in this repository alone does not establish matching source for that binary.

The checked-in Arch `PKGBUILD` needs a migration update: it still expects the
old `zuunedlinux` checkout directory and a libzune submodule. Do not use it as
a working installation recipe yet. Arch users can install the dependencies
listed in README and use the native CMake build instead. Report the actual
**About** version when testing a source build.

## First run and reports

- Choose a small music/video/photo fixture folder for the first scan. Skip
  onboarding if you first want to inspect the empty application.
- Metadata providers have bundled fallback keys and optional personal overrides.
  Provider outages are separate from local playback and manual editing.
- Local edits, playlists and playback work offline. Connect a Zune before
  dragging media or adding to the transfer queue. Start with one short fixture.
- Do not close the application during a transfer. After an interrupted transfer,
  follow the recovery message before playing a potentially incomplete object.

Report the complete AppImage filename and SHA256, the **About** version, distro
and glibc version (`getconf GNU_LIBC_VERSION`), X11/Wayland session, GPU, Zune
model, exact steps and expected/resulting behavior. Include a screenshot for
layout issues.

### Debug reports

The app saves recent logs automatically. After reproducing a problem, choose
**Settings → Health → export debug report** and save the text file. Use
**open report** to review it, then attach it to your report. It includes build
and system details plus recent logs, including earlier sessions after a restart.
Common credentials and home paths are masked; media names and other personal
text may remain. Nothing is uploaded automatically.

**open logs** opens the private local log directory (normally
`~/.local/state/Zuuned/Zuuned/logs`). Retention is bounded to eight 2 MiB files.
If the app cannot start, the loader or AppImage runtime may fail before logging
begins. Capture that output using the same `candidate` filename from above:

```bash
"$candidate" 2>&1 | tee zuuned-launch.log
# If FUSE is unavailable:
APPIMAGE_EXTRACT_AND_RUN=1 "$candidate" 2>&1 | tee zuuned-launch.log
```

See [DIAGNOSTICS.md](../docs/DIAGNOSTICS.md) for report contents and limits.

Zuuned is an independent community project, not affiliated with or endorsed by
Microsoft. Zune is a trademark of Microsoft Corporation.
