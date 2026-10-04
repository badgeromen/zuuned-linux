# Music metadata and artwork audit — 2026-09-07

Read-only audit of the current checkout. No production files, live library, settings, device, or provider services were changed or queried. No shared build or app restart. Findings below distinguish native reproductions from code-level limitations.

Reproduction commands and preserved diagnostic sources are in the [evidence guide](evidence/README.md).

**2026-09-08 alpha follow-up:** the original findings below remain the historical
audit. The app now imports trimmed container/selected-stream tags through the
shared native probe, preserves reliable tagged ownership, retains disc/year,
and distinguishes failed reads from untagged media. Schema v6 records successful
probe fingerprints/parser version; a normal scan repairs old automatic rows
once while keeping manual metadata and pinned collection identities intact.
Failures retain the last good row/fingerprint and appear quietly in Settings →
Library for retry. Album playback and transfer queues follow disc/track order.
The migration backup now includes committed WAL pages through SQLite's backup
API. See [`tests/music-import/README.md`](../../tests/music-import/README.md)
for isolated native fixtures, limits and verification. This correction covers
findings 1–4, whitespace handling in 5, and missing disc/year/fingerprint support;
case-folded artist grouping and the artwork-source/provider gaps below remain
separate work. No new on-device disc/dedup behavior is claimed.

The architecture is sensible: tags feed a local SQLite library, album ownership uses ALBUMARTIST, explicit Customize choices are pinned, and artwork discovery does not automatically rewrite identity. The highest-impact accuracy problems are earlier in the scanner, before online matching sees the name.

## Fix first

1. **P1 — Ogg/Vorbis and Opus lose valid tags. Confirmed with real files.**
   Both are accepted audio formats, but native `zune_probe` reads only `fmt->metadata`. These formats put music tags on the audio stream. Generated Ogg and Opus files containing title, artist, album artist, album, date, track and genre imported as filename `file`, folder artist `Codec`, folder album `Ogg`/`Opus`, track 0. `ffprobe` confirmed that the tags existed under `streams[].tags`. This sends incorrect names into grouping and portrait lookup.
   Sources: [probe metadata](/path/to/zuuned-linux/libzune/src/util.c:434), [accepted formats](/path/to/zuuned-linux/src/library/LibraryScanner.cpp:18).
   Bounded improvement: merge container tags with the selected audio stream's tags before filesystem fallbacks; preserve explicit tag precedence. Add native FLAC/MP3/M4A/Ogg/Opus fixtures. The shared probe correction belongs in libzune with its normal submodule discipline.

2. **P1 — A failed re-probe destroys previously good automatic metadata. Confirmed.**
   After a successful scan, replacing the temporary audio with an unreadable partial file and advancing mtime changed `Original Title / Original Owner / Tagged Album / 150 ms` into `probe / Folder Owner / Folder Album / 0 ms`. Probe failure is indistinguishable from a successfully probed untagged file; the scanner still commits it and advances mtime. A temporary read failure can therefore persist bad fallback metadata, and ordinary scans will skip it thereafter. User-edited text fields are protected, but duration is not.
   Sources: [probe result handling](/path/to/zuuned-linux/src/library/LibraryScanner.cpp:111), [unconditional resolved-row merge](/path/to/zuuned-linux/src/library/LibraryScanner.cpp:344), [upsert protection/overwrite rules](/path/to/zuuned-linux/src/library/LibraryDb.cpp:350).
   Bounded improvement: return explicit probe success/failure; keep the previous successful row and successful-probe fingerprint on failure, surface an unreadable/retry state, and retry later. Do not mistake “no tags” for “could not read tags.”

3. **P1 — Disc cleanup changes legitimate album names and cannot preserve disc order. Confirmed.**
   A tagged album `World 2` imports as `Worl`: the suffix regex permits `d 2` without a word boundary. Separate `Double (CD 1)` and `Double (CD 2)` become one album, but disc numbers are never stored. Four tracks then read `disc 2 track 1, disc 1 track 1, disc 2 track 2, disc 1 track 2` in the native DB ordering; QML also sorts only by track number. Consolidating discs is reasonable, losing their order is not.
   Sources: [suffix regex](/path/to/zuuned-linux/src/library/LibraryScanner.cpp:145), [track shape](/path/to/zuuned-linux/src/library/LibraryTypes.h:8), [DB ordering](/path/to/zuuned-linux/src/library/LibraryDb.cpp:323), [album playback ordering](/path/to/zuuned-linux/qml/MusicPage.qml:219).
   Bounded improvement: require an actual separated disc marker; retain a disc number from tags or an unambiguous `CD1` folder, then order by disc and track. Do not strip meaningful title suffixes without preserving what they represented.

4. **P2 — Folder inference can take ownership away from a valid tagged artist. Confirmed.**
   `Music/Downloads/21/song.flac` tagged `ARTIST=Adele`, `ALBUM=21`, without ALBUMARTIST imports with album owner `Downloads`. That becomes the Artists pivot name and the cover/portrait lookup identity. The stated final fallback “no TPE2 → track artist” never executes because the folder has already filled ALBUMARTIST. This works for a strict Artist/Album hierarchy but silently misclassifies other common layouts.
   Sources: [folder precedence](/path/to/zuuned-linux/src/library/LibraryScanner.cpp:123), [final fallback](/path/to/zuuned-linux/src/library/LibraryScanner.cpp:153), [owner-based grouping](/path/to/zuuned-linux/src/LibraryService.cpp:657).
   Bounded improvement: track provenance and agree an explicit missing-ALBUMARTIST policy. Prefer a reliable tagged performer over an unverified folder owner; preserve real compilation album artists and do not split featured tracks blindly. Show ambiguous fallback cases for correction instead of treating folder guesses as database identity.

5. **P2 — Whitespace and case rules disagree across import, grouping and portrait display. Confirmed import; display consequence follows directly from the keys.**
   Whitespace-only title/artist tags remain `" "`, blocking fallback. `Spaced Artist ` retains its trailing space in the DB. The portrait service trims that name before emitting success, while the UI looks up the original lowercased name with its trailing space, so a successfully fetched portrait can remain invisible. Separately, `CASE Artist` and `Case Artist` create distinct artist groups but share the lowercased album/cache identity.
   Sources: [raw tag import](/path/to/zuuned-linux/src/library/LibraryScanner.cpp:113), [artist versus album keys](/path/to/zuuned-linux/src/LibraryService.cpp:650), [portrait trim](/path/to/zuuned-linux/src/library/ArtistImageService.cpp:121), [published portrait key](/path/to/zuuned-linux/src/LibraryService.cpp:153), [UI lookup](/path/to/zuuned-linux/qml/MusicPage.qml:565).
   Bounded improvement: trim tag boundaries, treat whitespace-only tags as missing, and use one case-consistent identity key with a separately preserved display spelling. Existing cache and customization keys need compatible lookup/migration; do not simply rename the hash recipe or aggressively merge punctuation/diacritic variants.

## Artwork-specific gaps

6. **P2 — Automatic album art can choose the wrong image or miss available art. Four isolated native reproductions confirmed the selection/cache behavior.**
   An arbitrary `back.jpg` wins before an available embedded front cover. `/Album/cover.jpg` is ignored for `/Album/CD1/song.flac`, even though scanner metadata correctly skips the disc folder. A miss on the first representative track suppresses trying a second track with embedded art for the entire session. Once an automatic cover is cached, changing its source sidecar does not refresh it.
   Sources: [known images → arbitrary images → embedded chain](/path/to/zuuned-linux/src/library/AlbumArtService.cpp:76), [single representative track](/path/to/zuuned-linux/src/LibraryService.cpp:669), [cache/negative memo](/path/to/zuuned-linux/src/library/AlbumArtService.cpp:158), [facade cache fast path](/path/to/zuuned-linux/src/LibraryService.cpp:613).
   Bounded improvement: explicit custom cover → recognized front/cover sidecar at the album root → embedded front art from a bounded set of album tracks → arbitrary sidecar only last. Share the scanner's recognized disc-folder rule. Record automatic source/fingerprint separately from a user pin; expire failures after album changes, and refresh changed automatic sources without touching custom artwork.

7. **P2 — Album Fanart browsing still stops at the first name-search candidate. Code-level finding.**
   Without a pinned album MBID, the album Fanart path uses `matches.first()`. If the first release group has no cover but another plausible result does, the gallery reports no art. This is the album counterpart of the artist candidate aggregation fixed recently. It does not silently change identity, because browsing remains staged.
   Source: [album Fanart selection](/path/to/zuuned-linux/src/LibraryService.cpp:2217).
   Bounded improvement: aggregate a small, deduplicated set of plausible release groups with candidate identity visible, or require a release-group choice first. Preserve an exact pinned MBID. Add first-empty/second-success and mixed-error fixtures.

## Other concrete limits to account for

- **Year, disc number and embedded MusicBrainz IDs are not represented by `ZuneMetadata`.** Tagged 2011/2021 FLAC years imported as 0. Existing embedded stable IDs therefore cannot prevent same-name artist searches. [Probe contract](/path/to/zuuned-linux/libzune/include/zune.h:534). Year/disc/ID support is an additive shared-library + DB/API change, not a UI parsing tweak. Explicit Customize selections must keep precedence over file tags.
- **Rescan uses second-resolution mtime alone.** A rewritten file with a longer title and restored mtime kept `Before Retag`; its new tags were skipped. Consider size + higher-resolution successful-probe fingerprint and an explicit force-reprobe action. [Fast path](/path/to/zuuned-linux/src/library/LibraryScanner.cpp:237).
- **Filename fallback is intentionally narrow.** Untagged `01 - Adele - Hello.flac` became title `Adele - Hello`, Unknown Artist and track 0. It does not separately parse a track prefix followed by Artist–Title. Add only clearly specified filename shapes, preserving uncertain text. [Parser](/path/to/zuuned-linux/src/library/LibraryScanner.cpp:90).
- **Same owner/title means the same album, regardless of edition, year or folder.** Distinct reissues with identical tags share grouping and artwork. This is the present model, not a verified wrong match in the user's collection. An optional release/edition identity would require a deliberate model change. [Album identity](/path/to/zuuned-linux/qml/MusicIdentity.js:18).
- **Automatic names alone cannot establish cross-provider identity.** For an unpinned artist, one MusicBrainz match with no usable Fanart/Deezer relationship can still fall through to a unique same-name Deezer result; a MusicBrainz search error can also reach that fallback. Those catalogs need not mean the same person. Treat this as an inferred false-match risk, not evidence that a live portrait is wrong. Keep automatic fallback relationship-based once an MBID is known, and prefer embedded stable IDs when available. [Fallback policy](/path/to/zuuned-linux/src/library/MusicIdentityClient.cpp:560).

## Verified protections and evidence

`bash tests/music-identity/run.sh` passed **51 checks, 0 failures**, with injected provider responses and isolated caches. It verifies explicit stable-ID lookups, homonym rejection, exact Deezer matching, provider response validation, candidate aggregation for artists, and late automatic artwork protection during Apply. These safeguards should remain intact. It does not cover scanner formats or album-art source precedence.

The scanner reproductions used **15 generated audio files**, the existing `build/librarytool scan <temporary-folder> <temporary-db>`, and read-only queries of that temporary DB. Evidence: [fixture](evidence/music-scanner.mjs), [recorded results](evidence/music-scanner-results.json). The isolated artwork fixture compiled the real `AlbumArtService.cpp` and used a deterministic stub for embedded extraction, with real Qt image files/cache/worker behavior: [fixture](evidence/album-art.cpp). No provider or hardware inference was presented as live validation.

Suggested sequence: fix scanner correctness and conservative merge rules first; unify identity keys with compatible persisted-key handling; then improve automatic artwork source selection. Optional pop-art presentation can remain separate from the authoritative metadata and original artwork.
