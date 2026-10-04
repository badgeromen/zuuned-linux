# Native Sleeve tests

Run from the repository root:

```sh
bash tests/customize/run.sh
```

Requires Qt 6's `qmltestrunner`, Qt Quick Test, and the same QML modules as
the app. The runner links the production customization components into a
temporary QML module and supplies a recording `LibraryService` boundary.
It exercises the actual sheet's draft state and controls; it does not
reimplement them in a test fixture.

The tests run offscreen with isolated configuration/data/cache paths, make
no network requests, and never start ZUUNED or access a device. The software
renderer verifies interactions and geometry; final appearance still needs
a visual check with the app's normal graphics backend.

Provider-switch checks cover cached results and scroll positions, joining
in-flight requests, inactive-source replies, search/identity isolation, explicit
error retries, and bounded first-five preview retention. Song editor checks
exercise the production fields, title focus, save payload/tag notices,
compilation artwork, cancellation, and narrow-window footer access.

Additional arguments pass to `qmltestrunner`, for example `-v1` or an
individual test function name.

The real C++ Apply worker has a separate integration gate. Build the app
first, then run:

```sh
node tests/customize/run-backend.mjs
```

This uses Node's built-in modules to reuse the Ninja build's compiler/link
arguments and app objects, replacing only `main` with `backend.cpp`. An
optional argument selects a different Ninja build directory. It adds no
app runtime flags or package dependencies.

The fixture creates a `LibraryService` under unique temporary XDG roots,
with no videos or watch folders and only locally generated images and
inert FLAC fixture files. It never constructs `DeviceService`, starts the
app UI, or accesses the user's library. The temporary executable, database,
configuration, and caches are removed afterward.

Assertions cover combined album rename/art selection at the new cache key,
preservation of identity-only artwork and unrelated albums, atomic
rejection of invalid artwork, state already committed when success is
signaled, and artwork reset preserving metadata while resuming the actual
folder-art fallback.

The same backend gate also checks structured track-save errors (including an
actual SQLite rejection and a deleted row behind a stale UI snapshot), all
disconnected transfer-add APIs, offline playlist creation, and pruning confirmed
device-deletion snapshots without connecting to any device.

For a real playback-queue gate:

```sh
node tests/customize/run-backend.mjs --player
```

It generates silent WAVs and uses libmpv with null audio output. Play Next must
preserve the current song and pause/position, match the native playlist order,
and actually select the inserted song on Next. The selected queue entry must
finish loading before playback starts, without an older load replacing it.
The gate also covers shuffle's manual Next and natural EOF, repeated Play Next
requests, pause/Previous/jump during loading, rapid queue replacement, and
recovery from missing or corrupt selected files.

The artwork HTTP transport has a controlled-provider integration gate:

```sh
node tests/customize/run-backend.mjs --http
```

It links the same production transport and runs a temporary loopback HTTP
server, without contacting external providers. Assertions cover HTTP503
and429 recovery, bounded retries, numeric and HTTP-date `Retry-After`,
shared MusicBrainz pacing across concurrent lookups, permanent errors,
clearing stale error messages, and long provider cooldowns. It takes about
15 seconds and requires permission to bind a local socket. The HTTP gate
does not instantiate the library or device services.

MusicBrainz pacing follows its [official rate-limiting guidance](https://musicbrainz.org/doc/MusicBrainz_API/Rate_Limiting):
requests from one IP should average no more than one per second, and
overload or throttling can return HTTP503.

The runner uses `/usr/lib/qt6/bin/qmltestrunner`; set `QMLTESTRUNNER` if your
Qt 6 install uses a different path. The Qt 5 executable cannot run these
components.

For six native layout captures (including Disc and Vinyl) using the design study's sample artwork:

```sh
CUSTOMIZE_TEST_INPUT=tests/customize/capture.qml bash tests/customize/run.sh
```

The PNGs are saved to `/tmp/zuuned-sleeve-native-*.png`. The default software
renderer cannot render the image masks or gradient heading. For an actual
graphics check in a desktop session, set `CUSTOMIZE_TEST_RENDERER=opengl`
and `CUSTOMIZE_TEST_PLATFORM=xcb`. This opens temporary test windows and
should be run deliberately; it still uses the recording service.

Set `CUSTOMIZE_TEST_INPUT=tests/customize/capture-track.qml` for desktop and
narrow song-editor captures (`/tmp/zuuned-song-editor-*.png`).

The music completion cases cover stable provider IDs, correlated identity
searches, exact artwork seeding, persistent matched metadata, artist renames
carrying album choices, and atomic rejection of destination conflicts. The
backend also checks offline genre/mixtape art, restart persistence, rename,
reset, stale collections and failed image preparation.

`node tests/customize/run-backend.mjs --music-live` is an optional **live-network**
gate: it uses the production MusicBrainz/Fanart/CAA clients with temporary caches
and the bundled project key. It never constructs LibraryService or DeviceService.
Ordinary tests use fixtures; see `tests/music-identity/README.md` for provider and
portrait-race coverage without live requests.
