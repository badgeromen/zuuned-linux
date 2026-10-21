# Spoken media foundation gate

Run `bash tests/spoken-media/run.sh` from the app checkout. Requires the normal
Qt Core / FFmpeg development dependencies, a C++20 compiler, and FFmpeg CLI for
generating disposable test files. There is no USB, HTTP or personal-profile
access. The script cleans its temporary folder and fails on any failed assertion
or ASan/UBSan/leak finding.

The real `PodcastFeed` parser is exercised with RSS fixtures covering identity,
namespaces, Unicode, enclosure/art URL resolution, dates, duration, repeated
titles, a repeated GUID (the first item stays), RFC 5005 next and
previous-archive links, the longest and heaviest single documents measured
(24,258 episodes; 10,000 over 64 MiB), malformed XML, DTDs,
input/count/depth/text limits and unsupported enclosures. IDs are subscription-scoped: the parser does not merge
different feeds or persist subscriptions. RSS 2.0 is supported; Atom is reported
as unsupported. A nonmedia item increments `skippedItems`. Multiple distinct
playable enclosures currently require resolution instead of guessing one.

The real `AudiobookProbe` reads generated AAC-in-M4B with two chapters and a
tagged multipart MP3, preserving tags and chapter boundaries. It also rejects
URLs, directories, remote playlists and pre-cancelled input. Unknown disc/part
tags remain zero; missing chapters stay absent. No file content is modified.

The probe runs on a caller-owned worker. Its deadline/cancellation can interrupt
FFmpeg and subsequent reads, but cannot interrupt an already-blocked kernel
filesystem call. Successful inspection does not certify codec decoding, device
playback, audiobook category, DRM, sync, or resume behavior.

53 checks pass on the native development host (October 20). The `spokenmediatool` CMake target
uses these same implementations to produce local JSON inspection results. It is
a developer harness, not a runtime dependency or shipped tester application.
The first native UI/storage/download increment has a separate gate below. Protocol
tests remain separate: `bash libzune/tests/run-podcasts.sh`.


## Native application gate

Run `bash tests/spoken-media/native/run.sh`. It builds real SpokenMediaService,
SpokenStore, PlayerService/mpv and the native QML pages, using the normal Qt Quick,
Qt Test, SQLite, FFmpeg and mpv development dependencies. All settings, media and
databases use disposable `/tmp/zuuned-spoken-native-*` profiles. HTTP is loopback
only, mpv audio uses the null output, and DeviceService is a disconnected stub.
There is no device/USB access. ArtworkRequest is a rendering stub; production
artwork processing retains its independent regression suite.

Checks cover database transactions and reopen/future-schema behavior, subscription
and episode identities, streamed downloads, manual artwork and stale saves,
actual native Customize Apply/Cancel, ordered book imports, chapter boundaries,
bookmarks, local mpv resume, missing-file refusal, stop preservation and cancellation.
Native page screenshots are retained with the profile; the latest path is recorded
in `/tmp/zuuned-spoken-native-last.txt`. The test rejects QML runtime warnings.

October 20: loopback feeds paged three to a document (`/paged`, `/flaky`,
`/loop`, `/paged-big`) cover the background walk through older pages, catch-up
after new episodes, a failed page resumed by the next refresh, a walk resumed
when the library reloads, a page chain that loops, episode pages, filters,
search and starred episodes from SQLite, and the real show page scrolling a
250-episode show 25 at a time, keeping its place through a library change,
flipping to oldest first, selecting all and searching. `/ratio` serves a gzip feed compressed about 300:1, past Qt's
own bomb check; `/capped` fills a show to the 100,000-episode cap. `/repeat`,
`/slow`, `/dupe`, `/empty` and `/stuck` cover a chain repeating a page, cancel
and quit during a walk, a stale duplicate GUID on an older page, an empty page
and a page that always fails. 238 native checks plus 23 planner checks pass.

These gates do not establish physical device transfers, hardware artwork, native
desktop portal behavior or production audio/GPU behavior. Device queue integration,
audiobook protocol proof and packaging acceptance remain separate work.


The October 9 extension loads the actual QueueRibbonBar and NowPlayingIsland.
It tests spoken/music control switching, real mpv speed, sleep expiry and reset,
chapter navigation, preservation of music modes, and folder import grouping,
natural ordering, symlink exclusion, batch rollback, native Apply and repeat
reuse. The folders use generated files only. VisualizerService and
VideoPlayerService are inactive stubs; they do not certify those subsystems.
