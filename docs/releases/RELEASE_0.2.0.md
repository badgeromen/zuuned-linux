# Zuuned Linux 0.2.0

Prerelease notes, prepared October 13, 2026. Package checksums and completed
validation belong to the final release manifest; these notes describe the
candidate’s intended public scope.

## Podcasts and audiobooks

- Subscribe to podcasts by RSS, download episodes, customize covers, star shows
  or episodes, and manage their device copies in the Zune island.
- Choose newest, oldest or individual episodes to queue. Optionally queue the
  newest unplayed episodes from starred shows when a Zune connects, with a
  configurable count and per-show preferences. Transfers still require Sync.
- Optionally remove firmware-marked-played podcasts on the next sync. Removal
  actions appear in the queue first. Keep on Zune protects individual episodes;
  local downloads remain separate. Unknown device state preserves media.
- Import audiobook files or folders, organize books into Libraries with the
  familiar drag-to-island workflow, and customize covers. Removing a Library
  preserves its books and source files.
- Queue whole books. Chaptered sources are prepared as ordered device parts
  during sync, preserving the source timeline and original files.
- Spoken playback uses the existing top ribbon or Now Playing island, with
  appropriate skip, speed, chapter, bookmark and sleep controls. Setup offers
  spoken-media defaults that can be changed later in Settings.

## Playback and presentation

Short or narrow windows use Now Playing instead of a ribbon overlapping the
library. The island has a visible top-right X that turns red on interaction,
and Escape dismisses it when it has focus. Resizing while playing or paused
adapts the player; advancing a track does not reopen a dismissed island.
Podcast and audiobook covers use the application’s rounded artwork style.

## Downloads and testing

The release plan includes x86_64 and experimental aarch64 AppImages, built from
the same reviewed source. ARM64 requires a 64-bit desktop system; the Debian 13
build baseline is not a promise of compatibility with older distributions.
Zuuned bundles libzune, so application users do not install it separately.

Classic podcast audio/covers and small multipart audiobook hardware tests have
existing evidence. HD podcast playback/artwork, firmware audiobook resume,
long-book transitions and other model-specific behavior remain separate gates.
Played-state reads and removal previews have hardware evidence; end-to-end GUI
cleanup deletion still needs separate acceptance on Classic and HD.
The earlier ARM candidate launched and connected to a Zune on the user’s Pi;
that result does not certify the new release’s complete playback/sync workflows.
The layout matrix is a software test, not Bazzite compositor certification.
DRM-protected books, video podcast transfer and HD artist backgrounds are not
supported by this update.

Report the app version, distribution, desktop/session, CPU architecture, Zune
model and reproduction steps with a reviewed diagnostics report from
Settings → Health. See [tester installation and logs](../../packaging/TESTER_INSTALL.md).

Application: https://github.com/badgeromen/zuuned-linux

C library: https://github.com/badgeromen/libzune

Project website: https://zuuned.com
