# Player layout audit, 2026-10-12

**Follow-up:** the October 13 implementation below addresses compact-window
ribbon migration and island dismissal. The original findings are retained as
baseline evidence.

The top playback ribbon can overlap the library header in short windows. This
reproduces the layout problem visible in the user's screenshot. It does not yet
establish which overlay the Bazzite tester meant or reproduce an unclosable video
player.

## Build and method

Tested the current private `feature/podcasts-audiobooks` checkout: application
13ca27d28510 plus existing local changes, libzune 853bf3cde69c, Qt 6.11.2.
The native build completed successfully. No production behavior was changed.

[The reusable harness](../tests/player-layout/README.md) loads the application's
actual Main.qml and native playback services, with generated local audio/video,
a nine-item queue and isolated empty library profiles. Both Disc and Vinyl styles
were tested for music, podcasts, audiobooks and video. Each configuration uses
Qt fullscreen state on an offscreen virtual screen. Video additionally enters
and leaves fullscreen from windowed state using F.

The instrumented test executable uses software rendering. It verifies layout and
input handling, not hardware video rendering or compositor behavior. Shader
artwork/title/grunge effects are absent in captures. No USB device, live profile,
user window or desktop resolution was changed. The visualizer and photo viewer
were not tested.

## Display matrix

| Simulated display | Scale | Actual logical window | Header behavior |
| --- | --- | --- | --- |
| 1280×720 | 100% | 1280×720 | Collapsed; ribbon overlaps library tabs |
| 1366×768 | 100% | 1366×768 | Collapsed; ribbon overlaps library tabs |
| 1920×1080 | 100% | 1920×1080 | Collapsed; ribbon overlaps library header |
| 2560×1440 | 100% | 2560×1440 | Expanded; see tall-ribbon caveat below |
| 3840×2160 | 100% | 3840×2160 | Expanded; see tall-ribbon caveat below |
| 2048×732 | 100% | 2048×732 | Collapsed; screenshot-like overlap |
| 3840×2160 | 200% | 1920×1080 | Same layout problem as 1080p |
| 1920×1080 | 150% | 1280×720 | Same layout problem as 720p |

112 screenshots were captured. Actual logical dimensions, fullscreen state and
pixel ratio were recorded. The two scaled cases were rerun with QT_SCALE_FACTOR
after detecting that the installed offscreen plugin ignored the screen JSON's
`dpr` field; final results confirm DPR 2 and 1.5 respectively.

## Findings

1. **Ribbon and header do not share a height budget.** Main.qml collapses the
   header below 1100 logical pixels in height, moving the library tab band to
   y=74. The ribbon remains anchored at y=40 and only retires based on width
   below 1280. Its bottom is y=218 for music/Disc, y=243 for spoken/Disc,
   y=253 for music/Vinyl and y=278 for spoken/Vinyl. It therefore occupies the
   library header and content area in the short cases. On expanded layouts,
   the tab band starts at y=230: music/Disc clears it, while taller variants
   still extend into that vertical band. Bounding-band overlap alone does not
   prove a tab is covered at every width because much of that band is empty.
2. **Now Playing has no Escape dismissal.** All 48 audio/island Escape checks
   left the island open. This matches the absence of an Escape handler in
   NowPlayingIsland.qml, rather than proving a Bazzite-specific input failure.
3. **The island's small bottom close link worked in all 48 click checks.** At
   720 logical pixels, spoken/Vinyl puts the lower edge of its text about three
   pixels beyond the inner ColumnLayout clip. Its center remained clickable.
   This is a cramped/discoverability problem, not an established dead end.
   Strict rectangle-containment results can also flag fractional rounding at
   larger sizes; use the screenshots and actual click outcomes together.
4. **Video exit paths worked.** All 16 Escape checks and 16 × click checks
   stopped the video overlay. All 16 F-key round trips entered fullscreen and
   returned to windowed state. These results do not certify the Bazzite
   compositor, Steam Gaming Mode, controller navigation or GPU rendering.

## Recommended correction

Make header/content placement account for the active ribbon's actual height,
or migrate it to the island when vertical space is insufficient. Width alone
cannot decide this. Preserve the existing ribbon/island design and playback.
Give Now Playing an obvious persistent close control outside the scrolling or
clipped content, and handle Escape with appropriate focus/modal priority.
Re-run this matrix after those changes, then verify on Bazzite with the tester's
actual overlay, version, display scale and desktop/gaming mode.

## Local evidence

Captures, JSON geometry/input outcomes and console logs are under
`build/player-layout/<case>/`. These generated artifacts and isolated profiles
are intentionally not tracked. Useful starting points:

- `720p/disc-music-ribbon.png`: tabs and ribbon collide.
- `wide-short/disc-music-ribbon.png`: matches the user's short-window scenario.
- `720p/vinyl-audiobook-island.png`: cramped footer but working close link.
- `720p/vinyl-video.png`: visible video close control at 720p.
- `4k-200pct/results.json`: verified 1920×1080 logical size at DPR 2.


## Compact playback correction, 2026-10-13

The user chose to use Now Playing on short displays and a top-right X. After
trying the translucent red circle, they requested a plain X that changes color.

- Main.qml now retires the ribbon below the existing 1100-logical-pixel header
  breakpoint, as well as below 1280 logical pixels in width. This follows the
  same space decision as the header and naturally includes display scaling.
- Starting playback in a compact window opens the island. Resizing into compact
  mode also opens it for an existing queue, including paused playback. An active
  builder is not interrupted. Ordinary track changes do not reopen a dismissed
  island. Expanding while the island is closed restores the ribbon; an island
  the user has left open stays open.
- The new 32-pixel top-right button sits outside the clipped content. It has an
  accessible name and tooltip. The glyph turns red on hover, press or keyboard
  focus; its background is empty. The bottom close link has been removed.
- Now Playing is a FocusScope with Escape dismissal. It takes focus when opened
  and releases it when closed. This is a scoped key handler, not a global Escape
  shortcut that would compete with video or modal dialogs.
- The spoken footer no longer reserves space for the removed close link. The
  first header row reserves vertical clearance for the X; music's save-as-playlist
  action remains at the bottom.

The native application builds successfully. All eight display configurations
pass 304 behavior checks, with 112 new captures. These cover automatic
selection, close visibility/clicks, Escape, resizing while playing/paused,
staying closed through a track change, and video exit/fullscreen regressions.
The initial expansion assertion sampled at exactly the 400 ms animation duration
and caught a frame still fading; allowing the transition to settle resolved it. New automatic-layout captures use the suffix
`-automatic.png`; the original `-ribbon.png` captures remain useful baseline
comparisons. Other captures/JSON are overwritten by subsequent runs.

The running desktop application has not been restarted, and no AppImage or
public release has been produced for this change. Bazzite compositor/GPU testing
and user acceptance remain separate from the software layout gate.
