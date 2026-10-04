# Native music identity and portrait gate

```sh
bash tests/music-identity/run.sh
bash tests/music-identity/run-cache.sh
```

The script builds a temporary Qt C++ executable from the production
`MusicIdentityClient.cpp` and `ArtistImageService.cpp`, including the real Qt
worker, image decoder, cache publication, and generated signal metadata. It
does not rebuild or start the app. Provider JSON and image downloads are
injected; unexpected use of the default network transport aborts the fixture.
All caches live in temporary directories. No library or Zune is opened.

Coverage includes stable MusicBrainz release-group/artist IDs and Deezer IDs;
album artist credits, year, genre, and disambiguation; exact Fanart/CAA lookups;
ambiguous artist names; provider errors and malformed data; persistent cache
reuse and expiry; decoded-image validation; and automatic portrait completion
after Apply, identity replacement, reset, new custom artwork, or destruction.
Artist Apply pause tests also cover an authoritative destination name learned
while saving, requests queued before/during that save, failed-save recovery,
and repeated pauses while an invalidated worker is still running.

Fanart regression fixtures include its v3 HTTP200 `{}` no-art response for
artists and albums, persistence with the short empty-result TTL, and mixed
same-name results where an absent or failed sibling cannot hide valid portraits.
Malformed documents remain errors. The separate cache gate exercises concurrent
requests, transient failure cooldowns and stale positive results with injected
responses; it waits for one real cooldown expiry.

The existing `node tests/customize/run-backend.mjs --http` gate tests the
production shared `ArtworkHttp` transport's actual HTTP429/503 retries,
Retry-After cooldown, and one-request-per-second MusicBrainz pacing. This
fixture verifies that every new MusicBrainz search/detail uses that transport
classification; it does not replace the HTTP gate.

## Integration contract

`MusicIdentityClient` is a synchronous worker-local value object. Its default
fetch calls `ArtworkHttp::get`; the optional constructor fetch exists for
isolated tests. Identity maps use lowercase `provider` (`musicbrainz` or
`deezer`), string `providerId`, and MusicBrainz `mbid`, plus display fields.
Artist results include disambiguation and stable IDs even when names collide.
Deezer detail returns `portraitUrl`, `posterUrl`, and `artworkUrl` aliases for
the exact selected identity. Artwork methods return `QStringList`.

Provider JSON persists under the application's `music-identity-v1` cache:
24-hour positive results, five-minute empty/404 results, at most 512 files and
64 MiB. Requests for the same URL and cache directory share an in-flight lookup.
Valid positive results remain eligible for six more days during temporary
outages, without renewing their expiry. Empty results and malformed or permanent
errors cannot trigger stale fallback. Definitive identity rejection invalidates
old cached metadata. A bounded in-memory failure cache pauses repeated requests
for 30 seconds, or longer when required by `Retry-After`.
Invalid responses and transient failures are not stored as successful lookups.
Cache filenames hash the full request; stored envelopes contain only
expiry and response JSON, not API key URLs. The shared HTTP error logger must
remove query strings when printing provider URLs.

`ArtistImageService::requestImage(name, selectedIdentity)` uses selected IDs
before name search. Automatic lookup requires a unique normalized match.
Detected homonyms or truncated ambiguous search results do not choose a first
artist. A selected MusicBrainz identity uses Fanart; Deezer fallback for that
explicit identity requires a MusicBrainz URL relationship identifying the
Deezer artist. Name-only fallback requires one exact Deezer result and skips
placeholders. Artist portraits never substitute album artwork.

Call `ArtistImageService::setCustomizationPaused(true)` on the UI thread
**before** an artist Apply/reset starts its worker. This invalidates and retains
active/queued portrait requests, including a destination name that only the
authoritative provider response will reveal. New requests can queue during the
pause, and existing cached images remain readable. On success, call
`invalidate(name)` for each final canonical artist name before checking its
cache or requesting its authoritative saved identity. Finally call
`setCustomizationPaused(false)` on both success and failure so unaffected
portraits resume. `invalidate(name)` alone remains suitable for a known-name
change; it cancels that name's work without deleting cached files. Changed
identities also invalidate the previous generation automatically. Downloads
decode on workers; only the current UI completion may atomically publish the
complete JPEG, and an existing custom cache file is never replaced.

## Provider references

The [MusicBrainz API documentation](https://musicbrainz.org/doc/MusicBrainz_API)
defines release-group/artist search and detail lookups, optional genres and
annotations, meaningful User-Agent identification, and the one-call-per-second
limit. The client shares the existing application's limiter.

The [official Fanart client](https://github.com/fanart-tv/fanart.tv-api) documents
artist artwork by MusicBrainz artist ID and album artwork by release-group ID.
The native client uses those v3 endpoints and the application's existing
Fanart developer-key fallback or Settings override. No Node package or paid
metadata service is introduced.

The [Cover Art Archive](https://coverartarchive.org/) supplies exact
release-group front-cover results. Deezer uses the same existing keyless
artist API as the prior portrait fallback.
