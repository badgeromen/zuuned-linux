# Artwork print treatments and parsing audit

Design exploration requested 2026-09-07 on `ux/6-customize-design`.
The first delivery was a visual study and an audit, followed by a
[working local preview](../tools/artwork-preview/README.md) of all three
treatments using actual cached artwork. The main application now has an
Appearance picker and shared local display pipeline. The standalone preview
remains a development comparison tool; its earlier proof results are preserved
below. The native build, integration tests and Appearance capture pass.

## Visual direction

The user proposed optional pop-art treatments derived from an item's selected
artwork, with control over the palette and amount of detail. The target is
screen-printed gig posters and independent record sleeves: strong shapes,
recognizable subjects, source-derived colors, and restrained print texture.

The comparison sheet is in
[`mockups/artwork/01-print-study.png`](../mockups/artwork/01-print-study.png).
It compares original, clean ink, halftone, and worn print at large and small sizes.
These are generated concept treatments, not measured output from a local filter.
The inputs are the existing fictional Sleeve sample portrait, album, and series
art. Their provenance is recorded in
[`mockups/customize/assets/PROMPTS.md`](../mockups/customize/assets/PROMPTS.md).
The first attempt using a real artist portrait was rejected by the image
generator, so that reference was replaced with the fictional study assets.
Prompt and generation details are in
[`mockups/artwork/README.md`](../mockups/artwork/README.md).

| Treatment | Intended character | Main quality risk |
|---|---|---|
| Clean ink | Four or five coherent color/tone regions; restrained edges | Eyes, lettering, and small silhouettes can disappear |
| Halftone | Adjustable comic dots, color or black ink, with expanded print contrast | Large/strong screens can dominate faces or shimmer at thumbnail size |
| Worn print | Image-specific chipped edges, surface rubbing and sparse creases/scratches | Dense grain dirties faces; maximum wear can overwhelm the image |

The accepted direction uses clean ink as the base, modest optional texture,
and original artwork as the default setting. Each artwork supplies its palette;
grayscale sources remain neutral. Accent recoloring is a separate possible
feature, not part of this implementation.

## Appearance integration — 2026-09-08

The user approved shipping the lighter treatment from the live Alien comparison:
Detail **100**, Texture **14**, Halftone Dot size **17**, color ink. In
**Settings → Appearance → Artwork print**, underline text choices select
**original / clean ink / halftone / worn print**. Original remains the default;
choosing a print is optional. A before/after pair uses already available local
artwork, preferring the current album. Opening this section starts no provider
lookup.

**fine-tune** starts collapsed, exposing only the selected look's controls:

| Look | Controls | Reset defaults |
|---|---|---|
| Original | None | Original artwork |
| Clean ink | Detail | 100 |
| Halftone | Print strength, Dot size, color / black & white | 14, 17, color; Detail stays 100 |
| Worn print | Wear | 14; Detail stays 100 |

`AppSettings` stores each look independently across restarts. **reset this look**
restores only the selected print's defaults; switching styles retains the others.
Selecting Original restores the source display immediately without discarding
saved print preferences. Stored percentages are bounded to 0–100; malformed
settings use safe defaults. No device connection is required.

`ArtworkPipeline` handles local decoding, processing and derived cache work;
`ArtworkRequest` provides reactive, correlated subscriptions to the shared work.
`ArtworkImage.qml` keeps the chosen original URL as its public source while
displaying the prepared variant. Selected local artwork uses this component on
album, artist, poster, player, queue, genre, mixtape and Customize surfaces.
Shape masks remain outside the treatment. Photos, wallpapers, app marks and
remote provider candidates retain their original rendering.

This is a display preference: original artwork bytes, identity metadata,
Customize draft choices and device-transfer sources are never replaced by the
print cache URL. The pipeline uses bounded workers and caches with shared work
and stale-result suppression. Source artwork remains available while a print is
prepared. No network service or external processing dependency is involved.

**Integration verification passed, 2026-09-08.** The settings-specific gate
`bash tests/artwork-print/run-settings.sh` passes eight actual `AppSettings`
checks, including persistence in a fresh process, malformed stored values and
isolated reset, plus three production-QML interaction cases for sliders,
keyboard input, ink choices, collapsed controls and narrow layouts. These tests
use isolated config/data/cache roots and no library or device services. The QML
test substitutes the display renderer. The separate production pipeline gate
passes 58 checks covering byte preservation, canonical renderer parity,
replacement, cache corruption and I/O errors, coalescing, cancellation, overflow,
source/display leases and safe eviction. The actual Qt Quick display gate passes
12 OpenGL cases including setup/cleanup: all three treatments match native pixels,
hidden masks and movie composites retain artwork/overlays, rapid sliders never
flash raw art, and Original wins over late workers. Its compact viewport and
capture dimensions guard against false passes from clipped or empty images.

The existing Customize/music/video/photo/playlist/feedback suites pass 117 checks.
The full app builds and opens on Settings without QML warnings. The production
Appearance controls were captured with the cached Alien artwork at the accepted
defaults; the final capture has no rendering warnings. This is a software/display
gate; device-transfer hardware behavior was not changed or re-tested.

Reproduce with `bash tests/artwork-pipeline/run.sh` and the commands in
[`tests/artwork-display/README.md`](../tests/artwork-display/README.md).
The shared wrapper switches image opacity instead of child visibility so hidden
mask sources repaint. It retains the last ready print while rendering and loading
its replacement, with native display acknowledgments keeping both PNGs protected
from eviction. A new item or Original clears that previous treatment immediately.

## Native proof and implementation history

**Local proof completed, 2026-09-07:** `PrintRenderer` and the standalone preview
implement the first step below. Six actual cached portraits/covers/posters were
processed without network access or rewriting originals. The preview supplies
live detail/texture controls, source palettes, disk variants and small thumbnails.
**Physical reference supplied, 2026-09-08:** a handled VHS cardboard sleeve
clarified that worn print needs rubbed coating and sparse creases/scratches
across the surface as well as chipped edges. Each artwork must age differently.
Worn now seeds rub/scuff/crease placement from original decoded pixels, keeping
the pattern stable across controls and reopenings. Surface wear extends inward
with bounded contrast; faces and lettering remain readable. Actual Billie,
Skyfall and Alien previews were inspected at normal tile sizes. 34 renderer and
19 controller checks pass, including image-specific masks, stable placement,
surface wear and central contrast limits. Numerical difference alone cannot
establish the visual style. See the tool README for verification and reproducible
build/export commands, measured limits, and the remaining production integration.

**Comic halftone, 2026-09-08:** the user supplied monochrome comic and colored
process-dot references. A Halftone-only Dot size control and color/black-and-white
choices drive native tonal screens. Texture sets print strength; dot size
sets screen spacing. The source tone range is expanded for a clear comic print,
and source-derived colors separate into process inks. Other styles are unchanged,
and their cached variants are reused when these controls change. Actual color
and monochrome portraits were visually checked; see the preview README for
commands, validation and limits. These findings came from the separate study
window before the Appearance integration above.

**Worn surface correction, 2026-09-08:** the user identified square/QR-like
scuff patches in the portrait. Surface wear now uses warped gradient noise and
directional rubs instead of thresholded square cells. Chipped edges and seeded,
repeatable wear remain intact. The preview scrolls in short windows to keep the
artwork large enough to judge both treatments.

The original implementation plan used the app's existing local artwork URLs,
shared album/Customize entry points, native worker patterns and reactive
`AppSettings` preferences:

1. Build a small deterministic C++/QImage proof using actual cached portraits,
   lettered album covers, and posters. Extract a representative palette, simplify
   tone regions, then add optional texture. Measure the result and cost before
   selecting production parameters. No generative model is required for this
   proposed runtime pipeline; matching the concept quality is still unproven.
2. Store treated images as derived cache entries beside the source cache. Key
   them by source revision/content, algorithm version, palette, strength,
   texture, and output-size bucket. A changed artwork choice must invalidate its
   variants even if it reuses the same source filename.
3. Generate on a bounded worker pool with shared in-flight requests, cancellation,
   and bounded disk/decoded-image caches. Show the available source image while
   a variant is prepared. Avoid per-pixel JavaScript or repeated processing on
   scrolling, resizing, and tab switching.
4. Route the relevant QML surfaces through the same display treatment, including
   detail pages and Customize previews. Apply shape masks after the treatment so
   artists remain circular, covers square, and posters tall. All control styling
   uses `Theme.qml` and the approved underline-tab language.
5. Store the setting under Appearance with a live sample and immediate return to
   original. This is a display preference, separate from identity matching,
   metadata editing, and sync source artwork. A future export/save-stylized-art
   action would be its own explicit choice.

Integration acceptance must include real album lettering and faces at 48–96 px, grayscale
sources, dark covers, transparent sources, replacement at the same path, cached
and uncached scrolling, rapid setting changes, offline use, and cancel/reset.
Contrast and texture need to remain readable alongside ZUUNED's existing grunge.
An illustrative generated board alone cannot pass these implementation gates.

## Parsing audit

The artwork provider outage fixes are separate from interpreting media identity.
The audit examines the inputs sent into matching; it makes no claim that a
parser change can resolve an HTTP503 outage.

Detailed reports and reproducible examples:

- [Music tags, grouping, and artwork](audits/2026-09-07-music-parsing.md)
- [Video paths, matching, and enrichment](audits/2026-09-07-video-parsing.md)

Prioritize preserving source metadata and conservative identity extraction before
changing broad matching scores. Start with fixtures for the confirmed failures,
then fix their bounded causes. Reprocessing existing rows needs an explicit policy
for parser version/root changes and incomplete probes, with the existing manual
identity and custom-art protections preserved.

No production parser fixes are included in this study. Shared C probe/decoder
changes belong in libzune under its submodule workflow; Qt scanner/path-context
changes belong in the application. Multi-episode representation and broader
matching policy need their own design rather than additional ambiguous regexes.
