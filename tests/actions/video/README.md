# Video interaction checks

Run `bash tests/actions/video-run.sh` from the repository root. The runner loads
production movie/series/library video QML with recording service substitutes in
an isolated temporary Qt module and XDG directories. It does not start ZUUNED,
open the real library, contact providers, or use a Zune.

The checks cover:

- Real pointer click, context menu, and drag gestures on wide/narrow episode
  stills in both library and device mode; the device layouts expose save actions
  and hide local playback, watched, and queue-for-Zune controls.
- Connected-only transfer actions, including a disconnect after the view opens;
  editing, watched state, and local playback remain available offline.
- Separate device/library IDs: device episode commands cannot call local watched
  or playback paths.
- Current metadata, artwork, path, and category at transfer/play invocation;
  stale/deleted row references cannot queue, play, or open a customization draft.
- Movie hero context/drag and version scope, plus Needs Match individual/group
  context menus, connected dragging, and single/bulk customization context.
- Starting, updating, and stopping background scanning preserves the video
  gallery's position and dimensions, with no progress strip reserving space.

These are interaction regressions, not hardware transfer or rendering claims.
The normal software-renderer GL capability diagnostic is harmless; tests should
emit no QML warnings.
