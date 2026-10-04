# Zuuned Linux 0.1.0

A Linux x86_64 development build for the testing group, featuring the new
Specimen Alien display font. Existing graffiti-font selections switch to it
automatically; Permanent Marker remains the default. This build also includes
the current photo album and nested-album organizer.

Download `Zuuned-Test-0.1.0-x86_64.AppImage`, its matching `.sha256`, and
`TESTER-README.md` from this release's assets. Follow the guide to verify the
download, make it executable, and launch it.

**Requirements:** Linux x86_64, glibc 2.41 or newer, X11/XWayland and working
OpenGL. Built on Debian 13 with Qt 6.8.2. Older distributions and native Wayland
are not certified.

**Feedback:** reproduce the issue, then use **Settings → Health → export debug
report**. Review the saved text before sending it to the test coordinator or
attaching it to an issue. Include reproduction steps, expected/actual behavior,
Linux desktop details and Zune model. Reports are never uploaded automatically.
See the tester guide for collecting logs when the app cannot start.

**Verified for this artifact:** bundle dependency checks; isolated desktop
startup, onboarding, music import, native folder/open/save dialogs, diagnostics
export, settings persistence and nested photo-album creation/restart. The actual
AppImage also launched on the development desktop and read the connected Zune's
library. Physical transfer/playback and other desktop/device combinations remain
separate tests. Do not interrupt an active transfer.

**Build identity:** baseline app `426005d74320814f3341d7be8bfced52f854ed90` plus
uncommitted Specimen Alien changes (reported as `+dirty`); libzune
`d29bf7a5492b481f3211fbaeb3c596a00db7c742`, unchanged. This is a development
snapshot, not a clean tagged source build.

**SHA256:**
`82ffd7941fd4735f2d47a21f40fd5c81697a3136b1d78b2b8b5c391825517ed5`

## License and source status

Original Zuuned application code is GPL-3.0-or-later; see the attached
`LICENSE.txt` and `COPYING.md`. Third-party components retain their own terms.
The supplied notices archive contains notices from the exact packaged build;
completion of the remaining license material is still pending. The license
attachments accompany this build externally; the binary has not been rebuilt
since the application-license decision.

This is a draft pending the matching source-download link and completion of
remaining third-party license/provenance items. Application source will be
hosted separately, not committed to this downloads repository. GitHub-generated
source archives for this repository do not contain the application source.
