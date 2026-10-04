# Artist and album artwork matching plan

October 2 user correction: confidently identified albums automatically use the
general album cover when exact-edition art is unavailable or uncertain. Preserve
`generalAlbum` provenance and separate edition identities. Only uncertain artist
or album identity needs a match. This supersedes earlier explicit-general-cover
approval requirements below. Previously saved general-cover offers are retried
after restart; existing artwork and manual selections remain protected.

Status: native implementation candidate, October 2, 2026. The matching,
service, persistence and UI software gates pass. User acceptance and a new
AppImage/release remain pending. See MUSIC_MATCHING_ENGINE.md and the evidence
record below; this is not a claim that all release gates are complete.
Scope: local-library artist portraits and album artwork. Applies to first setup,
additional folders, background discovery, explicit retry and Customize.
Baseline: published Zuuned Linux 0.1.1. Keep that artifact available for rollback.

## Required behavior

- Artwork discovery never edits media files, display names, tags, grouping,
  playlist membership, transfer identity or device duplicate detection.
- Preserve local, embedded, device-imported and customized artwork. Existing
  valid cached art remains until an explicit user action replaces it.
- Artist IDs and provider-declared aliases take precedence over name spelling.
- Distinguish the album concept (release group) from a specific release/edition.
  Edition hints help match candidates; they never collapse separate albums.
- Prefer an established edition's cover. If only the album concept is known,
  offer its general cover explicitly; do not silently represent it as exact.
- Remember manual provider selections and show actionable unresolved states.
  Artist portraits never use album covers as a substitute for a portrait.

## Confirmed causes and architectural boundaries

The Poodle Hat provider response contains one result, score 100, credited to
`“Weird Al” Yankovic`, with an alias matching the library's `Weird Al Yankovic`. Automatic album selection
currently compares normalized credit strings and rejects that alias. Provider
search score alone is not proof of a correct release.

`MusicIdentityClient::searchAlbums` queries release groups. Automatic selection
requires exact artist and full album title, so qualifiers such as [Clean] and
[CD & DVD] can prevent finding the parent album. Removing all bracketed text
would introduce false matches and lose useful edition evidence.

`OnlineAlbumArtService` currently reduces outcomes to image bytes and success/
failure. It cannot explain ambiguity, missing artwork or transport failures.
Customize already persists collection identity and artwork choices; extend the
existing model instead of building a second competing manual-match store.
Its explicit identity-edit path can change metadata. New artwork-only matching
must have a separate save intent so accepting artwork does not invoke tag edits.

Keep native workers, provider pacing, shutdown cancellation, cache bounds,
Customize staging, generation invalidation and atomic no-overwrite publication.
Keep libzune and sync identity policy outside this change.

## Phase 0: protect the working baseline and reproduce failures

Before implementation, create a dedicated branch from the current working state.
The checkout and libzune contain uncommitted changes: preserve an exact snapshot
and reviewed checkpoint; a branch name alone does not protect dirty files. Do
not clean/reset the checkout or update the submodule. Use a copied test profile,
never mutate the live library to manufacture fixtures.

Capture bounded, sanitized provider fixtures for Poodle Hat, Halestorm, One-X,
Famous, Clean/Deluxe/CD+DVD variants and same-name artists. Add negative examples:
different albums with similar titles, compilations, collaborations, meaningful
parenthetical titles, incomplete albums and conflicting tags. Record baseline
hashes of covers, source files and relevant library identity/membership values.

Gate: failures reproduced offline and preservation assertions in place for both
artists and albums. Native tests must fail for the actual missing behavior.

## Phase 1: result types, artist IDs and aliases

Introduce typed discovery results carrying status, provider IDs, identity scope,
reason, evidence, candidate art and retry information. Separate identity matching
from image retrieval so a resolved artist without a portrait remains resolved.

Use valid saved/manual IDs first. Incorporate provider-declared credit names and
aliases into artist matching; punctuation tolerance is candidate evidence, not
permission to merge arbitrary names. Resolve same-name artists with consistent
album/track credits and require a clear winner. Keep joint credits and Various
Artists distinct from individual performers. Honor incomplete search responses.

Feed the resolved artist ID into album lookup. Reject fallback images from an
unrelated same-name artist. Reuse successful resolution across that artist's
albums while invalidating it if the underlying identity changes.

Gate: Poodle Hat alias accepted; same-name, conflicting-credit, punctuation-only
collision, collaboration and truncated-search cases stay safe. Existing manual
artist IDs and portraits remain authoritative.

## Phase 2: album candidates and edition confidence

Build a read-only evidence object from existing library metadata: original title,
base-title candidate, recognized edition hints, artist IDs, available track
names/order/durations, disc information and year. Keep original values intact.
Unknown values and partial libraries are missing evidence, not contradictions.

Search parent album candidates, then fetch bounded release details for plausible
matches. Prefer existing valid release IDs. Separate artist, album-concept and
edition confidence. Compare track lists and ordering, known disc counts, dates
and supported edition evidence. Clean/explicit is not reliably represented in
every provider response; unsupported evidence must remain unknown.

Require an absolute confidence threshold and sufficient margin over competing
candidates. Contradictory artist IDs or edition evidence block automatic edition
selection. Set thresholds from positive and negative fixtures, not arbitrary
provider scores. Persist a concise explanation of the decision.

Gate: punctuation/alias differences and supported edition suffixes recover valid
candidates; standard/deluxe/clean editions remain distinct in the library and
sync. Missing tracks or dates do not force the wrong edition. Ambiguous examples
produce candidates for review instead of an automatic assignment.

## Phase 3: exact artwork, explicit general fallback and saved choices

Prefer local/custom/current artwork first, then CAA artwork for the established
release ID. If exact art is missing or edition identity remains uncertain, expose
a release-group image as “general album cover” for explicit acceptance. Retain
that scope in provenance; accepting it does not claim an exact edition match.
Keep Fanart and existing identity-linked artist providers as fallbacks. Evaluate
additional providers only against remaining coverage gaps and their API terms;
replacing MusicBrainz or adding a new provider is not required for this phase.

Extend the existing customization persistence for artist ID, release-group ID,
optional release ID, manual/automatic choice, artwork source/scope and evidence
fingerprint. Inspect existing JSON storage before deciding whether new tables
are needed. Any schema change requires a backed-up migration and rollback test.
Persist image provenance only after successful atomic publication. Old records
remain readable. Successful manual choices override future automatic discovery.

Artwork-only Customize selection remains staged until Apply. Cancel writes
nothing. Save changes artwork/match information only; explicit identity/tag
editing retains its current separate user intent. Reset artwork does not silently
forget a confirmed match; changing a match is explicit. Wrong-album artwork
borrowed deliberately must not become the item's canonical identity.

Gate: matches survive restart and subsequent imports; Apply/Cancel and stale
worker races preserve user choices. No edits to file tags, original titles,
album grouping, transfer dedup or existing covers from background discovery.

## Phase 4: visible status and predictable retries

Expose reactive status for artist portraits and album covers: checking local art,
looking up, ready, needs a match, no artwork available, provider unavailable and
general cover available. Distinguish offline/timeouts/rate limits from a successful
empty response. Pending workers must not leave permanent searching states after
restart, cancellation, removal or failed download/decode.

Use restrained status/action text on missing-art surfaces and in Customize;
preserve existing placeholder shapes and Theme tokens. Actions: choose match,
retry, choose local image, or review the general album cover. A resolved artist
with no portrait should say so, rather than asking the user to rematch it.

Retry transient failures with bounded backoff and provider pacing. Do not
repeatedly search permanent ambiguity/no-art outcomes until relevant evidence
changes or the user requests retry. Manual retry respects provider throttling.
Invalidate stale negative results when a new folder contributes useful evidence.

Gate: both artist and album UI states are truthful for success, ambiguity, empty
responses, HTTP 429/503, offline mode, invalid images and shutdown. A second-folder
import can improve matches without an application restart or a retry storm.

## Phase 5: integration, user testing and release

Run focused matching, local/device artwork preservation, Customize, migration
(if applicable), provider shutdown and QML status/action tests, then the repository
regression suite and native build. Use deterministic fixtures for correctness;
live-provider checks separately validate request formats and real coverage.

Test first setup, second-folder addition, repeated scans, manual selections,
restart persistence, partial albums, large libraries and interrupted lookups in
a copied profile. Verify baseline cover/file hashes and unchanged grouping,
playlist membership and device-dedup inputs. Avoid unnecessary repeat lookups.

Provide a native test candidate for user review before packaging. Build the exact
accepted candidate through the AppImage skill and rerun packaged runtime gates.
Document revisions, dirty state if applicable, SHA256, provider limitations and
remaining hardware/desktop scope. No release upload or Git push without the
required new approval. Preserve 0.1.1 and database backups for rollback; never
launch an older binary against a migrated profile without an approved restore.

Gate: user confirms real-library artist and album recovery; all required software
and packaging checks pass. Physical sync behavior is not certified by artwork
fixture tests. Do not silently replace the published 0.1.1 asset.

## Completion criteria

Poodle Hat resolves via the correct artist alias. Edition-qualified albums find
appropriate candidates without metadata changes. Exact versus general artwork
is distinguishable. Manual artist/album selections survive new imports/restart.
Unresolved portraits/covers have useful status and actions. Existing art, files,
manual metadata, library grouping and sync identity remain unchanged.

## References

- [Picard matching](https://picard-docs.musicbrainz.org/en/latest/config/options_matching.html)
- [Picard artwork sources](https://picard-docs.musicbrainz.org/en/latest/config/options_cover.html)
- [beets artwork ordering](https://beets.readthedocs.io/en/stable/plugins/fetchart.html)
- [MusicBrainz releases and editions](https://musicbrainz.readthedocs.io/en/latest/terminology/entities/release.html)
- [Current discovery](MUSIC_ARTWORK_DISCOVERY.md)
- [Customize contract](SLEEVE_CUSTOMIZE.md)

## Implementation evidence, October 2

Branch: `feature/artist-album-matching`. Pre-change snapshot and hash:
`build/testing/music-matching-baseline/source.tar.gz` and `source.sha256`.
The snapshot includes the dirty app and libzune state; no submodule reset or
update was performed. The published 0.1.1 AppImage is unchanged.

- Phase 0: protected snapshot and isolated fixtures; Poodle Hat failure reproduced
  against the baseline. A SQLite backup of the test profile is prepared for
  review, with 159 existing cover/portrait hashes recorded.
- Phases 1-2: 98 provider/matching checks pass. Native live Poodle Hat lookup
  resolves the group, artist portrait and explicitly selected release cover.
- Phase 3: existing collection identity JSON holds separate `_artworkMatch`
  and `_artworkDiscovery` records; no schema migration. Artwork-only selections
  do not invoke metadata edits. `_artworkChoiceRevision` protects manual Apply
  and reset from delayed discovery saves. Scanner guards ignore these auxiliary
  fields as metadata pins. Seven focused DB checks and actual Apply integration
  pass, including concurrent first saves and manual-reset races.
- Phase 4: 20 service checks and 63 QML checks pass. Current-generation typed
  results distinguish ambiguity, empty responses, transient failures and general
  covers. Existing placeholders remain; status and actions are visible. Unknown
  artist metadata produces an explicit needs-match state without a web search.
- Phase 5: the 32-group regression run passed; final affected build/provider/
  service/DB/Apply/QML checks also passed after review corrections. The CI script
  now includes the new discovery/persistence/Customize gates. User visual review,
  acceptance on the copied real library and new packaged-runtime testing remain.

Native candidate: `build/testing/artist-album-matching/zuuned`.
SHA256: `ddb3c8a713f4ac4ed4d0ec6a5c73e5c85a54d633bda0ad67eeabd7be78821078`.
Candidate/profile manifest and retained logs: same directory's `candidate.json`
and `evidence/`. The candidate was prepared but not launched; the current app
and original profile were not restarted or modified for this work.

Limits: automatic release selection requires a complete inventory of at most
8 releases, adequate track evidence, score >=85 and lead >=10. Large inventories,
unsupported edition hints and ambiguity stay reviewable rather than guessed.
No additional artwork provider was added. MusicBrainz/CAA/Fanart and existing
identity-linked portrait fallback remain the sources. Device sync policy was
not changed or physically retested. No upload or push was performed.
