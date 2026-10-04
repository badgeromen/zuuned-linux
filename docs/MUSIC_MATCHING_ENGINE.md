# Music artwork matching engine

October 2 user correction: confidently identified albums automatically use the
general album cover when exact-edition art is unavailable or uncertain. Preserve
`generalAlbum` provenance and separate edition identities. Only uncertain artist
or album identity needs a match. This supersedes earlier explicit-general-cover
approval requirements below. Previously saved general-cover offers are retried
after restart; existing artwork and manual selections remain protected.

Implementation candidate, October 2, 2026. This covers provider identity and
artwork selection in `MusicIdentityClient`, not library writes or device sync.

## Contract

`resolveArtistArtwork(name, context)` and
`resolveAlbumArtwork(artist, album, context)` return `MatchResult` with status,
reason, image URL, identity and optional candidate rows. Status is `ready`,
`needsMatch`, `noArtwork`, `providerUnavailable`, or `generalCoverAvailable`.
A general cover is an offer, not permission to publish it. A saved explicit
`choice=manual, scope=general` permits retrieving that confirmed general cover.

Album context can contain saved `releaseId`, `releaseGroupId`, legacy group
`providerId`, resolved `artistMbid`, `_libraryYear`, and `_libraryTracks` with
title, track, disc and duration **in milliseconds**. Artist context supports
saved provider identity and `_libraryAlbums`. These are read-only lookup inputs.

Provider-declared aliases are accepted. A single artist's alias cannot match a
joint credit. Resolved artist IDs constrain album searches with `arid`. A saved
release ID is verified against its release group. Artist portraits use Fanart
or the resolved MusicBrainz artist's explicit Deezer relationship, never an
unrelated same-name fallback. A resolved artist with no portrait retains its ID.

## Release evidence and bounds

Only recognized trailing qualifiers are separated from album titles: deluxe,
expanded, clean, explicit, CD plus DVD and remastered labels. Unknown parentheses
remain meaningful title text. Original metadata is never rewritten.

Release inventories must be complete and contain at most eight releases. Each
release detail is verified against the group and artist. Larger inventories are
not sampled for an automatic winner. Complete small inventories also provide
manual candidates with group/release IDs, title, artist, date, country and
scope. Missing metadata remains unknown. Conflicting known track titles,
durations differing by more than ten seconds, or unsupported edition hints block
exact automatic selection. Clean/explicit is often absent in provider metadata;
that absence does not establish either edition.

Evidence requires at least three matching track positions, or every track on a
shorter release. Score starts at 60, adds two per matching track (up to ten),
two per matching duration (up to five), 15 for a complete matching local track
list, ten for supported edition hints and five for matching release year.
Automatic exact selection requires at least 85 and a margin of ten over the
next candidate. Equal pressings remain ambiguous. Standard titles do not silently
claim releases explicitly described as deluxe, expanded, clean, explicit or
remastered. These conservative thresholds are covered by fixtures, not a claim
of universal music-catalog accuracy.

Exact covers use `caaReleaseUrls` and the release endpoint. General covers use
CAA release-group art with Fanart fallback. Identity scope is `exactRelease`,
`generalAlbum`, or `artist` and remains distinct from local album grouping.
Legacy URL-only entry points remain for compatibility with existing explicit
callers; background discovery uses the typed methods.

## Verification

`bash tests/music-identity/run.sh`: 98 checks, zero failures, offline injected
transports. New checks include Poodle Hat alias, recognized versus meaningful
suffixes, collaboration rejection, truncated search, 503, exact track evidence,
tied releases, unknown clean labels, conflicting tracks, manual release identity,
explicit general-cover reload, artist aliases/collisions, no portrait and saved
artist identity. Existing cache, retry, race and shutdown-adjacent tests remain.
Test log: `/tmp/music-match-engine-tests.log`.

A native live probe used only a temporary provider cache and no library/device:

- `Weird Al Yankovic / Poodle Hat` resolved to group
  `b89956ff-8ad4-3a21-b8b8-c3c064530855`, with general cover offered explicitly.
- Artist ID `7746d775-9550-4360-b8d5-c37bd448ce01` returned a Fanart portrait.
- The group produced five release candidates plus its general-cover candidate.
- Selecting release `1de82b5a-b1e9-41fa-b6ca-e6357ba082c7` returned ready with
  `exactRelease` scope and a Cover Art Archive URL.

Live evidence: `/tmp/zuuned-matching-v2-live.log`. This verifies real endpoint
formats and URL selection, not image decode, UI display, or hardware behavior.

Remaining coverage limits include large release inventories, missing provider
aliases, non-Latin/transliterated names without declared aliases, collaborations
requiring manual identity, inaccurate source tags and provider outages. The
engine does not fetch recording fingerprints or infer clean/explicit from audio.
User real-library review and packaged runtime gates remain separate.

The baseline failure was reproduced with the same injected alias fixture against
`/tmp/zuuned-before-matching-20261002/src/library/MusicIdentityClient.cpp`:
`baseline alias cover recovered=no (reproduced)`. The new typed method reports
`generalCoverAvailable` for the identical fixture. Evidence is in
`/tmp/zuuned-alias-repro.log`; both probes use temporary caches and no live
providers. Known extra tracks/discs and duplicate local positions are rejected
rather than ignored or allowed to inflate confidence. General-cover identities
remove release IDs; an explicit general choice wins over stale release fields.
