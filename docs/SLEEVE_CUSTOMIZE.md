# Sleeve customization

The user approved the Sleeve design study on 2026-09-06. This is an intentional
ZUUNED design change from the older boxed Customize form, implemented natively
in QML on `ux/6-customize-design` (based on `ux/6-customize`). The browser studies
under `mockups/customize` remain references; they are not part of the app.
Future design explorations default to wireframes unless a richer mockup is requested.

The subsequent action audit and connection rules are documented in
[`ACTION_AUDIT_FIXES.md`](ACTION_AUDIT_FIXES.md).

## Native experience

Right-click a local album, artist, movie, series, genre, or saved mixtape and
choose **Customize…**. Genres and mixtapes have one artwork pane; their names
remain in the existing library/builder flow. Genre art replaces the automatic
mosaic, while mixtape art becomes the cassette label. The card, drilldown, and
saved mixtape builder all read the same override, even without a Zune connected.
The sheet opens on Artwork: a large sleeve and vinyl edge for albums, a circular
artist portrait, or a 2:3 video poster. The app's selected marker/graffiti face
and orange-to-pink heading remain part of its identity. Metrics and colors are
in `Theme.qml`; images are rendered through `CustomizeArtwork.qml` and editable
liner notes through `CustomizeField.qml`.

Album sleeves use the same Disc/Vinyl components and live `Prefs.playerStyle`
setting as the player. The decorative disc has no playback connection, and its
entire rim stays inside the artwork column. Source, identity-mode, and media-type
tabs use underlines without button fills or focus boxes. Both searches share
the filled search-field component; the extra rule above the heading is removed.

Alternate images occupy one horizontal row, with wheel/trackpad scrolling,
dragging, previous/next arrows, and keyboard navigation. Selecting a cover
updates the large preview. The source search is independent of the item's
identity; a manually named video can borrow another title's poster.

While the sheet is open, each provider/search retains its results and scroll
position, including requests still in flight when the user switches tabs.
Returning to a source does not send another API request or clear the row. The
first five previews per search stay decoded with the same image options as
the visible strip. This session cache is capped at 12 searches and released
on close; choosing a different identity uses a separate cache key. Empty
results and errors are also retained. Search / Try again explicitly retries
a failed request without hammering providers during tab switches.

Missing artwork uses one opaque `Theme.artworkPlaceholder` color, keeping its
square, circle, or poster shape without emojis, glyphs, initials, or branding.
Local artist portraits never substitute album covers when no photo is available.
MusicBrainz identifies the artist, then Fanart.tv supplies artist thumbnails.
Ambiguous exact-name matches require a choice in Customize. A selected database
ID is authoritative; a same-name Deezer result cannot replace it. A linked
Deezer identity can supply a fallback portrait, and explicit Deezer selection
uses that artist's exact `picture_xl` / `picture_big` image. Existing cached
portraits remain until changed/reset. Device artists retain their representative
device artwork where no separate portrait is known.

The older song **Edit Info** sheet now shares the Sleeve heading, surface,
quiet liner-note fields, and anchored footer. It preserves the existing
single-track metadata/tag save path, numeric limits, and initial title
selection. Its compact song context uses the album artist for compilation
artwork. Long forms scroll inside the available window height.

Selecting a new identity seeds Artwork with its title and exact database ID.
Manual title changes also seed the artwork query. Prior artwork responses are
invalidated, while the user's selected cover remains in the draft. An explicitly
different artwork query still searches independently of that identity.

Identity and artwork share one draft. Switching tabs never writes to the
library. Apply submits one prepared save; Cancel, close, and Escape discard
the draft. While a save is running, a second Apply and dismissal are disabled.
Preparation or save errors keep the draft available for correction and retry.
Long forms scroll inside the sheet, keeping the footer in place. Short/narrow
windows use a compact artwork header.

## Persistence and scope

`LibraryService::customizeContext` seeds forms from authoritative library rows,
including metadata omitted by a context menu. Correlated artwork and identity
requests prevent a late response from populating a different item or search.

`applyCustomization` validates and prepares images and TMDB details before
mutating the library. It uses a worker-owned database connection and transaction,
atomic cache file replacement, and rollback copies for cache failures. A music
rename carries artwork to the new canonical cache keys. MPEG tag propagation
stays off the UI thread; failure to update a media file is reported separately
from a successful authoritative library save.

Video artwork has its own `custom_poster` pin in schema v3. Automatic matching
can still identify an unmatched video while preserving its chosen poster.
Every automatic matcher write respects manual identity, including work that
was already in flight when Apply completed. A manually identified movie can
appear with a placeholder until artwork is chosen.

Video **reset to auto** is staged until Apply and restores the shared scanner's
filename/folder interpretation before retrying matching. Music **reset artwork**
releases the cached image and resumes automatic artwork discovery. It does not
claim to restore original music tags that were overwritten by earlier edits.
Bulk series edits preserve each episode's numbering and individual title.

Music identity offers **write it yourself** and **find a match**. Albums match a
MusicBrainz release group; artists match MusicBrainz or Deezer. Search rows show
credits, dates and disambiguation so identical names can be distinguished. Apply
fetches authoritative details before opening the save transaction. Album shared
tags and artist credits remain sticky (`user_edited`); individual song names and
track numbers are preserved. MusicBrainz supplies metadata, not a biography feed;
the manual form includes liner notes that are saved with the collection.

Schema v5 stores collection identities and custom-art references. Renaming an
artist carries each affected album's database identity, notes and art override
alongside its new cache key. A collision with an existing customized destination
rejects the complete draft. Genre/mixtape images live under the library's
`custom-art/`, using stable playlist IDs across renames; reset restores the
automatic mosaic. Failed saves restore prior metadata and image files.

Artwork sources are Cover Art Archive/Fanart for albums, Fanart/Deezer for
artists, TMDB/Fanart/frame grabs for video, and local files. Chosen MusicBrainz
identities drive exact artwork lookups; typing a different artwork search remains
independent of identity. Discogs and paid providers are not enabled.

Provider JSON survives restarts with a bounded cache (24-hour positive results,
five-minute empty results, at most 512 entries / 64 MiB). Concurrent requests for
the same URL and cache share one lookup. Temporary outages reuse valid positive
results up to seven days old without extending their original expiry; empty
results, malformed responses and permanent errors never receive that fallback.
Failed requests have a 30-second cooldown (longer when `Retry-After` requires it),
so repeated searches do not restart the same failed lookup. Automatic artist
downloads validate/decode images on workers and publish complete files without
replacing user choices. Artist Apply pauses and invalidates automatic lookups
until the final canonical identity has committed, then resumes unaffected work.

Expanded editing of items already on the device is a separate remaining task;
this completion covers local library Customize. Custom photo albums and nested
subalbums are covered by `PHOTO_ALBUMS.md`; the phased feature sequence and
remaining hardware gates are in `FEATURE_COMPLETION.md`.

Provider access checked 2026-09-07: [Fanart](https://api.fanart.tv/) offers free
project access with a seven-day delay for new images; [MusicBrainz](https://musicbrainz.org/doc/MusicBrainz_API)
is keyless and free for noncommercial use. TheAudioDB is not a default because
its [free terms](https://www.theaudiodb.com/docs_terms_of_use.php) restrict app-store
publication to paid subscribers. API access does not confer ownership of images.

Fanart v3 returns HTTP 200 with `{}` for an unknown artist or album. Treat this
as a short-lived empty result, not malformed JSON. When an explicit artist search
finds multiple exact-name identities, available portraits remain usable even if
another candidate has no images or fails. If every candidate fails or has no
artwork, preserve the first provider error; an entirely empty search has no error.
A pinned MusicBrainz identity still requests only that exact artist.

MusicBrainz queries are paced across workers and retry temporary HTTP
429/503 responses with a bounded backoff. `Retry-After` is respected, including
server cooldowns longer than the automatic retry budget. An unavailable provider
produces a short actionable message; URLs and transport diagnostics stay in the
app log. Successful retries clear the earlier error.

## Validation

Provider correction gate, 2026-09-07: native build, 51 provider/portrait checks,
21 cache/coalescing/outage checks (including real cooldown expiry), and 43 actual
Customize/transfer assertions passed. The service fixture verifies clean empty
Fanart results, mixed same-name portraits and album artwork reuse of an identity
search. A live Fanart check confirmed HTTP200 `{}` for the second Billie Eilish
identity and 23 portraits from the combined search. The broader live run stopped
when a subsequent MusicBrainz artist lookup timed out; this is recorded as an
upstream availability failure, not a passing full live-provider gate.

Completion gate, 2026-09-07: full native build; 52 Customize/song QtTest passes;
38 Apply/transfer assertions; 38 provider/portrait assertions; 11 shared HTTP
retry/pacing assertions; 21 database Customize checks; and the existing music,
playlist and photo action regressions passed. Genre/mixtape/builder captures were
rendered and inspected with OpenGL. Playlist-specific storage/service/editor
gates are documented separately in `tests/playlist-order/README.md`.

A bounded live check using the production client and a temporary cache also
passed: three distinct Christopher Larkin identities, Skyfall/Adele album
details, three Fanart artist portraits, and one exact Cover Art Archive image.
This validates those endpoints at test time, not permanent provider availability.
Run deliberately with `node tests/customize/run-backend.mjs --music-live`;
it makes real provider requests but never opens the user's library or a Zune.

- `cmake --build build -j 4`
- `bash tests/customize/run.sh` — production QML with an isolated recording service.
- `node tests/customize/run-backend.mjs` — the actual Apply API against a
  temporary library and local artwork, including cache preservation and failure.
- `node tests/customize/run-backend.mjs --http` — controlled local HTTP responses
  for provider retries, pacing, cooldowns, and user-facing errors.
- `./build/librarytool customize` — isolated database transactions, pinning,
  migration, automatic-matcher races, and reset behavior.
- `./build/librarytool names` — shared filename/folder parser regression cases.
- `bash tests/music-identity/run.sh` — native provider/cache/portrait race fixtures.
- `bash tests/music-identity/run-cache.sh` — coalescing, stale fallback, temporary
  failure cooldowns and permanent rejection without live providers.
- `node tests/actions/music/run.mjs` — real genre/mixtape menus, offline actions,
  staged reset, and reactive covers.

The native sheet was also rendered with OpenGL to check the actual image masks,
gradient heading, and artwork shapes. The fixture tests do not connect to a Zune
or modify the user's collection. See `tests/customize/README.md` for details.

Initial gate, 2026-09-06: full build, 45 native UI checks, eight Apply integration
assertions, 21 database checks, and 17 naming cases passed. The updated app was
launched successfully; its existing migration path backed up the v2 library
before upgrading to v3. Live provider availability and real video frame sampling
are environment-dependent and were not part of the offline fixture gates.
