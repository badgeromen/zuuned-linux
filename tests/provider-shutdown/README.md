# Provider shutdown regression

Build the native application, then run:

```sh
node tests/provider-shutdown/run-backend.mjs
```

The runner links real native application objects with an isolated test main,
uses disposable XDG directories and a loopback HTTP server, and never opens USB
or the user's library. The server deliberately stalls two active requests; a
third request waits on the shared MusicBrainz mutex. Application quit sets the
terminal transport cancellation flag, then workers drain before QApplication
destruction. All three must cancel within one second, late calls must be refused,
and no application/event-loop lifecycle warnings may appear.

September 28 gate: six checks passed; worker drain measured 2 ms. Native build
passed. The fix covers ArtworkHttp provider requests (including MusicBrainz and
Deezer), pacing/retry waits, and pool lifetime. It does not add cancellation to
the independent TMDB transport, scanning or transcoding; their existing work
must still finish. Repeat the packaged copied-profile close test independently.
