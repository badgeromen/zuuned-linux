# Zuuned Linux 0.2.1

Tester prerelease, October 21, 2026. Includes libzune 0.3.0.

## Changes

- Large podcast feeds now have a bounded 128 MiB limit rather than 8 MiB.
  The reported A Celtic State of Mind feed loads 3,104 episodes; a separate
  Stuff You Should Know test loaded 2,912 episodes and retained their identities
  through refresh. This addresses the preview failure reported in issue #1.
- Bundled libzune 0.3.0 hardens database/reply parsing, external-tool invocation,
  temporary files, authentication allocation failures and transfer retry/cancel
  handling. It also protects against uncertain-write duplicate spoken uploads
  and deletion of tracks referenced by an HD album after an unreadable response.
- Current spoken-media transfer, artwork, recovery and queue fixes are included.
- Zuuned Sans provides an additional display-font choice.
- libzune exposes the new HD artist-content C API with examples and current API
  documentation. This is library functionality, not an automatic background
  artwork delivery feature in the Zuuned GUI.

## Downloads

The 0.2.1 AppImage targets x86_64 Linux, glibc 2.41 or newer, X11/XWayland and
OpenGL. It bundles libzune; application users do not install the library separately.
Official builds retain embedded Zune authentication. Public source omits private
credentials and the private visualizer. See BUILDING.md and docs/BUILD_WITH_MTPZ.md.

No new ARM64 binary is included in this release. The experimental 0.2.0 ARM64
release remains available, without these fixes. No macOS binary is provided.

## Validation and limits

The preceding private candidate used the same functional app and libzune changes.
Its exact AppImage passed clean Debian runtime and photo-builder gates and actual
GUI device testing: HD podcast transfer/readback and music/video pulls; Draco
removal/resync of seven audiobooks (213 parts), plus music, video, photo and
podcast transfers; and Keel transfer of a complete 20-part book, five podcasts,
music, a WMV video and a photo. Fresh device inventories matched those transfers.
Large-feed catalogue tests and six fully decoded HD pulls also passed. These
results are detailed in TESTER_HARDENING_2026-10-21.md. The release manifest
separately records checks of the new public-source 0.2.1 binary; earlier exact-file
hardware evidence must not be confused with testing every feature of that binary.

Known limitations:

- Importing videos already held locally produces duplicate episode cards and an
  inflated on-device count. The downloaded files are intact. This is in the backlog.
- Device podcast/audiobook browsers do not yet offer Save to Library.
- Physical playback, long-book transitions, firmware resume and completed-podcast
  cleanup still need broader model-specific acceptance. Scorpius is untested.
- A successful Debian runtime check does not certify all distributions, graphics
  drivers or audio outputs. The original Ubuntu issue reporter still needs to retest.

For problems, use Settings → Health → export debug report, review it, and attach
it to a GitHub issue with your distribution, desktop, Zune model and steps.

Application: https://github.com/badgeromen/zuuned-linux

Library: https://github.com/badgeromen/libzune
