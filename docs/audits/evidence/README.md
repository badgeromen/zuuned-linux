# Parsing audit evidence — 2026-09-07

These diagnostics record known shortcomings in commit `4fa6af0`. They print
observations rather than enforce the current bugs as passing regression behavior.
Production fixes should add assertions for the intended corrected behavior.
The copied C++ includes were made relative to `src`; their logic is unchanged.

Run commands from the repository root after building the app. The diagnostics
use temporary files/databases, generated silence, pure parsing helpers, or
injected artwork extraction. They do not open a Zune or the real library, or
query providers. Prerequisites are the app's existing build dependencies,
FFmpeg/ffprobe, sqlite3, and Node for the music diagnostic.

## Music tags and rescans

```sh
node docs/audits/evidence/music-scanner.mjs
```

This generates 15 tiny audio fixtures and a temporary SQLite library, calls the
existing `build/librarytool scan`, and records JSON at
`/tmp/zuuned-music-audit-fixture-results.json`. The diagnostic prints and retains
its unique scratch directory for inspection. The historical output is
[`music-scanner-results.json`](music-scanner-results.json).

## Video parsing and TMDB query preparation

```sh
c++ -std=c++20 -O0 -fPIC -Isrc -Ilibzune/include \
  docs/audits/evidence/video-parser.cpp src/library/VideoNaming.cpp \
  libzune/obj/util.o \
  $(pkg-config --cflags --libs Qt6Core Qt6Network libavformat libavutil) \
  -o /tmp/zuuned-video-parser-audit
/tmp/zuuned-video-parser-audit
```

The diagnostic includes `TmdbClient.cpp` to exercise its private query splitter;
do not link it a second time. It never constructs a provider client. The existing
libzune utility object requires libavformat/libavutil at link time even though
only filename decoding runs. Historical output:
[`video-parser-output.txt`](video-parser-output.txt).

## Album-art source and cache behavior

```sh
art_audit_moc="$(pkg-config --variable=libexecdir Qt6Core)/moc"
"$art_audit_moc" src/library/AlbumArtService.h -o /tmp/zuuned-art-audit-moc.cpp
c++ -std=c++20 -O1 -fPIC -Isrc -Isrc/library -Itranscode \
  docs/audits/evidence/album-art.cpp /tmp/zuuned-art-audit-moc.cpp \
  $(pkg-config --cflags --libs Qt6Core Qt6Gui) -o /tmp/zuuned-art-audit
/tmp/zuuned-art-audit
```

This uses the real artwork resolver, workers and cache, with an injected embedded
image extractor. Recorded stdout from the audit (exit 0):

```text
arbitrary sidecar chosen before available embedded cover: CONFIRMED (embedded calls=0, red=254)
album cover one level above CD1 is missed: CONFIRMED
first-track miss prevents trying second track with embedded art: CONFIRMED
automatic cache persists after source sidecar change: CONFIRMED
```
