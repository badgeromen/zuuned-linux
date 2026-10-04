# Zuuned Linux - tester guide

Zuuned manages music, videos, photos and playlists on Microsoft Zune players.
This is a development testing build. Start with a small test folder and a few
items before trying a large library or transfer.

## Download and run

Download these two files from the same entry on
[GitHub Releases](https://github.com/badgeromen/zuuned-linux/releases):

- `Zuuned-Linux-0.1.1-x86_64.AppImage`
- `Zuuned-Linux-0.1.1-x86_64.AppImage.sha256`

In a terminal opened in the download folder:

```bash
sha256sum --check Zuuned-Linux-0.1.1-x86_64.AppImage.sha256
chmod +x Zuuned-Linux-0.1.1-x86_64.AppImage
./Zuuned-Linux-0.1.1-x86_64.AppImage
```

The checksum should report `OK`. If it fails, download both files again before
running the app. No source checkout or compiler is needed.

If the launcher reports a FUSE error:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./Zuuned-Linux-0.1.1-x86_64.AppImage
```

This build is for **64-bit Intel/AMD Linux**, built on Debian 13 with Qt 6.8.2
and glibc 2.41. It uses X11 or XWayland and requires working OpenGL. Older
Ubuntu/Mint releases, native Wayland, ARM and non-systemd device setup are not
certified. Installing FUSE will not fix a `GLIBC_... not found` error.

File pickers use your desktop's `xdg-desktop-portal` FileChooser backend. Report
missing or unexpected dialogs together with your desktop environment.

## Connect your Zune

1. Open Zuuned and connect the player by USB.
2. If Zuuned shows a USB-permission card, select **Fix it for me**. This setup
   can ask for an administrator password; ordinary app launches do not need it.
3. Reconnect the USB cable if requested.
4. Try one short song first, then verify it on the player.

Keep Zuuned open and the USB cable connected during a transfer. An interrupted
send can leave an incomplete item on the player. Follow any recovery message
before trying to play that item.

Local playback, metadata/artwork editing and photo-album organization work
without a connected Zune. Adding items to the device transfer queue requires
one. Online metadata/artwork lookups require network access and may be affected
by provider outages.

## Send a useful bug report

1. Note the steps that reproduce the problem and roughly when it happened.
2. Open **Settings → Health → export debug report** and save the `.txt` file.
   If Zuuned crashed, reopen it and export promptly; recent sessions are kept.
3. Select **open report** and review the file before sharing it.
4. Send the file to the person coordinating your test, or attach the reviewed
   file to a [GitHub issue](https://github.com/badgeromen/zuuned-linux/issues/new/choose).
   Drag the file into the issue description. Add a screenshot for visual bugs.

Include this information:

```text
AppImage filename and release:
Build version (Settings → About, or the command below):
Linux distribution/version and desktop environment:
X11 or Wayland session:
Zune model (if relevant):
What I did, step by step:
What I expected:
What actually happened:
Approximate time of the problem, including time zone:
Does it happen every time?:
Debug report attached:
```

To print the exact build version without starting the app:

```bash
./Zuuned-Linux-0.1.1-x86_64.AppImage --version
```

The report contains recent logs and useful app/system details. It does not
include your music files or library database, and **nothing is uploaded
automatically**. Common credentials and home paths are masked, but media names,
mount paths and other personal text may remain. If the repository is public,
issue attachments are public too. Use your existing private testing channel
for material you do not want to publish. Never include passwords, API keys or
a full copy of your profile/database.

## If the app will not open

Start it from a terminal and capture the error:

```bash
./Zuuned-Linux-0.1.1-x86_64.AppImage 2>&1 | tee zuuned-launch.log
```

For a FUSE failure, try:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./Zuuned-Linux-0.1.1-x86_64.AppImage 2>&1 | tee zuuned-launch.log
```

Review `zuuned-launch.log` before sending it with your report. Terminal output
can include details that are not covered by the exported report's filters.

Automatic logs are normally in `~/.local/state/Zuuned/Zuuned/logs/`, or
`$XDG_STATE_HOME/Zuuned/Zuuned/logs/` if that environment variable is set.
**Settings → Health → open logs** opens the actual folder. Send the latest log
and the previous session's log if the problem happened before a restart. The
app retains at most eight log files of up to 2 MiB each. A loader failure can
happen before automatic logging starts, so terminal output helps in that case.

## What to test

- Import a small music/video/photo folder and check the displayed information.
- Play a song and a local video; try pause, seek and volume.
- Create a photo album and a nested album; add photos, save and reopen Zuuned.
- Change artwork or metadata and verify the saved result.
- With a Zune connected, send a small selection and check it on the player.
- Report duplicate artists/albums/tracks with the exact steps and debug report.

The release notes identify what passed for this exact build. Passing the
isolated desktop checks does not certify every device, graphics card or audio
setup. Device-browser video playback is not offered; device videos can be
saved to the local library.

Zuuned is an independent community project, not affiliated with or endorsed by
Microsoft. Zune is a trademark of Microsoft Corporation.
