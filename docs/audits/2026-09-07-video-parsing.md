# Video parsing and matching audit — 2026-09-07

Read-only audit of the current checkout. No production files changed, app restart, shared build, scan of user media, provider request, or device operation. Read `docs/PORT_PLAN.md`, `docs/VIDEO_TITLES.md`, Customize's override contract, and the decoder entry in `libzune/docs/TOC.md`.

## Validation performed

Reproduction commands and preserved diagnostic sources are in the [evidence guide](evidence/README.md).

- `./build/librarytool names`: all 17 checks pass.
- Temporary fixture [`video-parser.cpp`](evidence/video-parser.cpp) compiles the production `VideoNaming.cpp`, includes production `TmdbClient.cpp` solely to exercise its private query splitter, and links the existing libzune utility object. It calls only pure parsing/query helpers; no network client is constructed.
- Reproduction output: [`video-parser-output.txt`](evidence/video-parser-output.txt).
- “Confirmed fixture” below means observed output from that executable. “Confirmed code path” means the implementation establishes the behavior, but a complete scanner/matcher integration fixture was not run. Provider match outcomes are explicitly not claimed.

## Current pipeline and protections

1. Typed watch roots select eligible video extensions and size/bonus-folder exclusions (`src/library/LibraryScanner.cpp:180`, `:251`). Parsing happens only for new files or changed mtimes (`:279`). Video container probing is deferred, so initial identity comes from paths, not embedded video title tags.
2. `VideoNaming::parseIdentity` calls the shared C `zune_decode_filename`, then overrides category/series using the watch-folder type (`src/library/VideoNaming.cpp:16`). TV/anime always use a derived parent-folder series; movies explicitly clear episode identity. An `all` root uses filename inference.
3. Scanner writes series/season/episode/category (`src/library/LibraryScanner.cpp:375`). Matcher groups TV by lowercase series and otherwise searches movies (`src/library/VideoMatcher.cpp:107`). It chooses the highest score above 0.55, with no ambiguity margin (`:18`), then fetches details, posters, season episode metadata, and stills.
4. TV folder series remains separate from TMDB's display title. Automatic writes preserve manual identity through SQL `user_edited=0` guards and preserve a custom poster through `custom_poster` (`src/library/VideoMatcher.cpp:418`, `:439`, `:458`, `:488`). Scanner parse writes also respect manual identity (`src/library/LibraryDb.cpp:574`). These safeguards should remain intact.
5. Shared parsing also powers explicit reset-to-auto; reset picks the longest matching watch root (`src/LibraryService.cpp:2594`). A parser improvement therefore affects both new scans and deliberate reset.

## Highest-value confirmed gaps

### 1. Season folders supply the show name, but never supply season numbering

**Confirmed fixture.**

| Typed TV path | Actual result |
|---|---|
| `/tv/Show/Season 2/Show E03.mkv` | Show, **S1E3** |
| `/tv/Show/Specials/Show E03.mkv` | Show, **S1E3** |
| `/tv/Show/Season 2/03-Title.mkv` | Show, **S1E3** |
| `/tv/Show/Season 2/03 - Title.mkv` | Show, **S1E0** |
| `/tv/Show/Season 2/Show 2x03.mkv` | Show, **S1E0** |
| `/tv/Show/Season.2/Show.S02E03.mkv` | series **Season 2**, S2E3 |

The C decoder defaults season to 1 (`libzune/src/util.c:120`); episode-only patterns retain that value. The Qt layer only replaces series/category (`src/library/VideoNaming.cpp:26`). Its season-container regex sees the raw folder name and does not accept `Season.2` or `Season_2` (`:41`, `:190`). `2x03` is unsupported, and leading numbered episode handling requires a dash immediately followed by a letter (`libzune/src/util.c:328`).

Impact: the app can retrieve a real episode from the wrong season, or only show-level information when the episode number stayed zero. This is an identity problem before artwork lookup.

Bounded next step: distinguish explicit filename season from a default; only fill absent season from an unambiguous season/specials folder. Normalize separators before recognizing container names. Add `2x03` and spaced leading-number cases only with TV context, preserving movie-number safeguards.

### 2. Folder truth has no watch-root boundary and can overwrite a correct show name

**Confirmed fixture.** `/tv/Breaking.Bad.S01E01.mkv` and `/tv/The.Wire.S01E01.mkv` both become series **tv** under a TV watch root. `/media/Show/S01E01.mkv` under an `all` root becomes series **media**.

The TV override unconditionally replaces filename-derived series with `deriveSeriesFromPath` (`src/library/VideoNaming.cpp:26`); that helper receives no root and treats any parent name as a show (`:185`). In `all` mode, the C decoder's empty-prefix SxxExx fallback always tries the grandparent (`libzune/src/util.c:157`), even when the file lives directly in its show folder.

Impact: unrelated flat-folder shows merge into one group and share one TMDB query/artwork choice (`src/library/VideoMatcher.cpp:122`).

Bounded next step: pass the selected watch-root boundary into path derivation. A genuine show subfolder remains authoritative; the root itself must not replace a usable filename series. Use the nearest real show folder when no filename series exists.

### 3. Movie cleanup can erase the actual title or turn a title number into a release year

**Confirmed fixture, including production TMDB query splitting.**

| Filename | Actual TMDB title query | Separate year filter |
|---|---|---|
| `1917.mkv` | **empty** | 1917 |
| `1984.mkv` | **empty** | 1984 |
| `2001 A Space Odyssey.mkv` | A Space Odyssey | 2001 |
| `Blade Runner 2049.mkv` | Blade Runner | 2049 |
| `1917 (2019).mkv` | 1917 | 2019 — working control |
| `The Final Cut (2004).mkv` | **The** | 2004 |
| `Mission Impossible - FALLOUT.mkv` | **Mission Impossible** | none |

`splitTrailingYear` actually removes the last year-like token anywhere in the query, even the only title token (`src/library/TmdbClient.cpp:35`). Search methods turn that into an API year parameter (`:138`, `:150`). Edition terms such as `final cut` are removed globally (`src/library/VideoNaming.cpp:87`, `:263`); an all-capitals final subtitle is assumed to be a release group (`:248`). Existing cleaner tests protect mixed-case “Fallout,” not the same title in capitals.

Bounded next step: carry explicit year evidence separately from title cleaning; retain a title-preserving query variant when numbers are ambiguous. Restrict edition/release-group stripping to supported release-context positions instead of deleting ordinary title words globally. No claim is made about the particular candidates TMDB would return today.

## Confirmed lifecycle/artwork gaps from code

### 4. Reparse policy can leave old identity and new numbering inconsistent

- Changing **Treat As** only changes the watch-folder row and requests a rescan (`src/LibraryService.cpp:434`, `src/library/LibraryDb.cpp:1220`). Unchanged video mtimes are skipped (`src/library/LibraryScanner.cpp:279`), so existing files do not adopt the new type.
- For a changed file, `updateVideoParse` overwrites numbering/category without invalidating automatically matched metadata (`src/library/LibraryDb.cpp:574`). Example: absolute anime S1E1043 is resolved to a real later-season episode by `VideoMatcher.cpp:219`; a later mtime change reintroduces raw S1E1043 while the old episode title/still and `tmdb_cached=1` remain. The automatic lookup queue excludes cached rows (`src/LibraryService.cpp:1030`).
- Overlapping watch roots are walked independently using one initial path index (`src/library/LibraryScanner.cpp:255`). New duplicate paths attempt a second insert; there is no longest-root choice or work deduplication. This differs from reset's explicit longest-root policy. Actual conflicting-root fixture was not run.

Bounded next step: one deterministic root-selection and reparse policy shared with reset, plus isolated tests for Treat As, overlapping roots, and a previously mapped absolute episode. Do not bulk rewrite manually customized rows or discard pinned art.

### 5. Successful show matching can permanently hide a transient episode/artwork failure

**Confirmed code path; no provider outage was induced.** A season request failure produces an empty list (`src/library/VideoMatcher.cpp:313`). The caller then still writes `tmdb_cached=1` for every episode (`:181`, `:421`), even with no episode title/still. Poster download failure similarly leaves a successful series resolution with no poster (`:295`), which is memoized for the process lifetime (`:168`). Cached rows are not retried automatically (`src/LibraryService.cpp:1030`).

Additionally, permanent no-match series resolutions are memoized (`src/library/VideoMatcher.cpp:168`), and Retry Match resets DB flags without clearing that memo (`src/LibraryService.cpp:1647`, `src/library/VideoMatcher.h:97`). A retry of the same name can immediately reuse the old no-match result instead of querying again.

Bounded next step: separate identity success from pending episode metadata/art resources, and make explicit retry invalidate the relevant memo. Preserve an already correct identity and any custom poster while retrying only incomplete automatic work.

## Suspected matching risks / known unsupported shapes

- **Confidence risk, not a verified wrong live match:** no runner-up ambiguity threshold; first candidate wins equal scores. The fixture scores unrelated “Dark Matter” at 0.60 for query “Dark (2017),” above the 0.55 acceptance threshold, although “Dark” scores higher if present. Need a controlled result-list fixture before changing scoring globally.
- **Confirmed unsupported representation, user policy needed:** `S02E03E04` stores only episode 3; `ParsedIdentity` and `LibVideo` have a single episode number. Multi-episode files need an explicit representation/UX decision, not another regex alone.
- **Confirmed behavior, policy needed:** explicit season 0 is excluded from season metadata fetching (`src/library/VideoMatcher.cpp:181`). Distinguish a known specials episode from “unknown numbering” before deciding how to fetch it.
- **Mixed-root ambiguity:** `Star Wars Episode 4.mkv` under `all` becomes TV/S1E4; a movies root correctly forces movie/S0E0. Strengthening typed-root precedence is preferable to blindly expanding filename heuristics.

## Existing coverage and a bounded first improvement batch

The 17 `names` checks cover 14 normalization/cleanup examples, exact-vs-junk scoring, and one simple absolute mapping. They do not exercise `parseIdentity` itself, folder season propagation, root boundaries, numeric movie titles, `splitTrailingYear`, malformed provider payloads, or interrupted artwork enrichment. The separate Customize gate contains one parseIdentity/reset case with a fully explicit `S02E07` filename and actual manual/custom-art matcher write guards (`src/library/librarytool.cpp:133`, `:209`).

Suggested first batch, after the user's design decision: add a table of the supplied path fixtures; fix root-boundary/season provenance and conservative title/year extraction; then add an isolated DB test proving automatic reparse/retry preserves manual choices and does not mix old metadata with new numbering. Keep multi-episode modeling and a broad scoring rewrite outside that first batch.
