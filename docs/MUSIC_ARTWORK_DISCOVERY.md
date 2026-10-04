# Music artwork discovery after adding a folder

October 2 status: the initial multi-folder artwork fixes shipped in 0.1.1. Earlier candidate
notes below are historical. The artist/album follow-up is documented in
[MUSIC_MATCHING_PLAN.md](MUSIC_MATCHING_PLAN.md); its native candidate passes
software gates, with user acceptance and packaging pending.

Automatic album covers come from local folder images and embedded artwork.
Missing album covers now fall back to online lookup after all known local
sources fail. Customize remains the explicit match/artwork chooser. Artist
portraits use the online identity/artwork providers.

## October 1, 2026 correction

Previously, a miss on one representative track suppressed the entire album
for the session. Adding another folder or requesting another track could not
recover its cover. Concurrent requests for another source were also dropped.

LibraryService now indexes all track paths for each existing album cache key.
An on-demand cover request queues these alternatives. AlbumArtService serializes
extraction per album, remembers misses per source path, and stops once a cover
exists. Coordination belongs to the service instance and survives its workers
safely. Existing cache identities and no-overwrite behavior are unchanged.
This does not change album grouping or edition identity. Same-path artwork
changes after a miss still require explicit reset or a fresh application session.

Artist lookup failures previously expired after five minutes but did not
schedule another attempt. A failed request now receives one automatic retry
after five minutes, preserving request generations, customization pauses and
existing portraits. Persistent absence does not cause an endless retry loop.
Artist cards also request images when their model row changes; album art reacts
to artist and local/device source changes.

## Verification

- `bash tests/device-music-import/run.sh`: existing imported-cover protection,
  a miss in the first folder followed by a cover in the second, repeated-miss
  suppression, and a second source arriving while extraction is in flight.
- `bash tests/music-identity/run.sh`: provider/identity and custom-art safeguards,
  automatic recovery without another UI request, bounded retries and cancellation
  after identity invalidation. Provider responses and retry timing are injected;
  these tests do not depend on live services.
- `cmake --build build -j 4`: complete native application and QML compilation.

Live logs also showed a MusicBrainz HTTP 503 and failed music-file probes.
Retrying artwork cannot recover unreadable media or guarantee provider coverage.
The user's second-folder collection still needs an interactive retest. The
published AppImage predates this correction.

## Independent automatic artwork discovery — implemented on feature branch

The fresh onboarding profile `/tmp/zuuned-fresh-setup-3p441zy1` started with
empty data, settings and artwork caches. Its session log confirms that the
populating album covers were extracted from music files or loaded from image
files beside the music on the Kubeplex SMB share. This proves their immediate
source, not which application originally created those sidecar files.

The feature branch populates missing artwork without depending on a Jellyfin server:

1. Preserve explicit custom artwork and existing valid covers. Try recognized
   local cover files and embedded images before any remote album request.
2. Use the existing native MusicBrainz identity client and Cover Art Archive /
   Fanart clients for missing album covers. Saved provider IDs take precedence;
   name-only matches must agree on artist and full album title and reject
   ambiguity. Never strip deluxe, explicit, remaster or edition qualifiers to
   force a match. Release-group identification alone does not prove an edition.
3. Retain the existing MusicBrainz/Fanart artist portrait pipeline and its safe
   identity-linked fallback. Queue missing portraits when library content is
   added, with bounded background work rather than relying solely on visible
   gallery delegates. Both initial setup and later folder additions use this.
4. Downloaded images use Zuuned's existing album cache and unchanged keys.
   Log local extraction and online success/unavailability separately. Settings →
   Library → Music artwork → Find missing artwork rechecks uncached local
   sources and retries remote failures without deleting good images or changing
   source tags/shared-folder files. Full persistent provenance/status UI remains
   future work.
5. Online album requests are serialized. Provider transport retains its global
   pacing, caches and shutdown cancellation. Failed album requests have a
   five-minute cooldown; explicit retry bypasses this queue cooldown, while
   provider-level cooldowns still apply. No automatic album retry loop.
6. Local source changes invalidate pending downloads. Customize pauses online
   album publication and invalidates affected albums on successful saves. Only
   current UI-thread completions publish, atomically and without overwriting
   existing artwork. Album matching changes no metadata, grouping or device
   duplicate policy.

Validation: native build; 72 provider/identity/queue checks; 17 local/device
import checks, including a later embedded cover in an atomic local-source batch.
Live native lookup for Disturbed / The Sickness downloaded a valid 500×500 cover
(54,725 bytes). Fresh setup and adding another folder both schedule the same
background pass; interactive acceptance remains the user's test gate.
The AppImage release is unchanged; this is a native testing build.

Limitations: absent or ambiguous matches stay blank for Customize. Edition
qualifiers are retained, so provider titles lacking those qualifiers may not
match automatically. Existing valid cached artwork is kept, including previously
downloaded covers; later source-file changes do not silently replace it.

Reference behavior: Jellyfin accepts folder and embedded music images
(https://jellyfin.org/docs/general/server/media/music/) and offers external image
providers, including its Cover Art Archive plugin
(https://github.com/jellyfin/jellyfin-plugin-coverartarchive). No Jellyfin API or
cache integration is required for this plan.

Candidate rehearsal: the isolated profile was relaunched with
`build/testing/automatic-music-artwork/zuuned` (PID 516564, mapped on workspace 1).
All 81 pre-existing cached cover files matched their recorded SHA-256 hashes
after startup. Customize backend integration passed; provider shutdown passed
6 checks. This preserves the original regular profile and does not publish
or rebuild the AppImage release.

## Artist ambiguity correction

Live cache inspection showed six exact-name Disturbed artists, four Adele
matches on the first page, and a truncated Korn search (25 of 57 results).
Name-only ambiguity guards therefore left those portraits blank. Artist
requests now include an ephemeral list of the owner's local album titles.
For ambiguous/truncated artist results, up to three full album-title searches
can corroborate one candidate MBID through exact album-artist credits.
Conflicting credits, incomplete album responses and absence of evidence still
require an explicit choice. Saved provider identities remain authoritative;
corroborated MBIDs cannot fall back to an unrelated same-name Deezer artist.
This changes no stored tags, album grouping or user-selected identities.
Provider regression gate: 76 checks passed, including album corroboration,
conflict and incomplete-response rejection. Native application build passed.
