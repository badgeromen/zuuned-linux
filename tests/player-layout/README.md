# Player layout audit

Runs the application's actual Main.qml, player services, ribbon, Now Playing
island and video controls on simulated fullscreen displays. The runner fails if
the compact-window policy, close controls, Escape handling,
resize behavior or video fullscreen round trips fail. This is a software layout
gate, not a hardware/compositor release certification.

From the repository root, with the native Ninja build configured:

```sh
cmake --build build --parallel 3
python tests/player-layout/build.py
python tests/player-layout/run.py
# Or select particular cases:
python tests/player-layout/run.py 720p wide-short 4k-200pct
```

Requires the app's native build dependencies plus Qt Test, Python 3, FFmpeg,
Bubblewrap and dbus-run-session. No root permission is needed. The build script
reuses the native build's object files and resources, replacing only main.o with
an instrumented copy. Product sources and the normal executable are unchanged.
Rebuild the normal application first after changing production code.

## Coverage and evidence

The default matrix covers 1280×720, 1366×768, 1920×1080, 2560×1440,
3840×2160, a 2048×732 short window equivalent, 4K at 200% and 1080p at 150%.
The runner verifies the actual logical window dimensions, device pixel ratio
and fullscreen state. Scaling uses QT_SCALE_FACTOR because the installed
Qt offscreen plugin does not honor the newer screen-config `dpr` field.

Each display exercises music, podcasts and audiobooks with both Disc and Vinyl
styles, a nine-item playback queue, the top ribbon and the Now Playing island.
It checks automatic island/ribbon selection, island Escape dismissal and the
visibility and click behavior of the top-right X. It resizes while playing and
paused, checks return to the large-window ribbon, and verifies that a closed
island stays closed across a track change. Video tests capture
both styles, press Escape, click × and toggle fullscreen with F in both directions.

Outputs live in `build/player-layout/<case>/`: PNG screenshots, results.json,
console.log and isolated profile directories. JSON `reachable` means the entire
item rectangle fits within the window and ancestor clips. It is a geometry
observation, not an occlusion/hit-test verdict. The separate `*-close` records
are actual mouse clicks at the control's center. Containment allows a quarter
logical pixel of numerical rounding; it does not excuse visible clipping.

## Isolation and limits

The runner gives each case its own HOME/XDG paths, session bus, fake /dev and
/sys, network namespace and PID namespace. Only the audit directory is writable.
It never resizes the user's desktop or closes an existing app. Audio uses mpv's
null output; input media is generated locally. Real USB devices are inaccessible.

The instrumented main selects Qt's software scene graph and offscreen platform.
This exercises native layout, focus and input behavior, but **does not validate
OpenGL effects, rendered mpv video frames, hardware decoding, compositor input
routing or Bazzite Gaming Mode**. Shader-based titles, artwork effects and grunge
are absent in these captures. The library starts empty and the queue uses test
metadata, so screenshots establish geometry rather than production artwork.
Visualizer and photo-viewer behavior are outside this audit.

See [the October 12 findings](../../docs/PLAYER_LAYOUT_AUDIT_2026-10-12.md).
