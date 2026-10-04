# Existing-device title repair gate

After building the application, run:

```sh
node tests/video-titles/run-repair.mjs
node tests/video-titles/run-repair.mjs --capture
```

The runner compiles the current controller and test fixture against the native
application's existing objects. It creates a temporary SQLite library and a
recording QObject device, injects them into the actual QML sheet, and never
instantiates DeviceService or touches USB. `--capture` additionally writes the
native preview to `/tmp/zuuned-video-title-repair.png`.

Coverage includes exact normalized show/season/episode matching, absent episode
and season metadata, ambiguous remakes and duplicate library/device rows,
year-qualified legacy movie filenames, title punctuation, explicit selection,
one-at-a-time requests, correlated completion, partial failures, disconnects,
transfer/pull/purge guards, and fresh device/library validation before each
rename. The native sheet must load and render without QML warnings.

Repair uses metadata identity, not a byte comparison. Episodes require a
nonempty authoritative episode title. Movies require one matching library row;
multiple copies, conflicting matches, missing identity, and unknown video kinds
are intentionally excluded. Saved means DeviceService reported a successful
Name update; firmware display and persistence across reconnect still require
the hardware gate.
