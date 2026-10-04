# Local artwork print preview

A standalone Qt 6 preview of the actual deterministic C++ filters in
`src/artwork/PrintRenderer.cpp`. This is the working proof following the generated
artwork study. It does not link DeviceService, libzune, or online artwork clients,
and remains a separate development comparison tool. The main application now
exposes these treatments under **Settings → Appearance → Artwork print** through
its own shared local worker/cache pipeline. See
[`docs/ARTWORK_STYLE_STUDY.md`](../../docs/ARTWORK_STYLE_STUDY.md) for production
defaults, surface coverage and the completed integration verification gate.

The main app defaults to Original; its optional looks start at Detail 100,
Halftone strength 14 / Dot size 17 / color and Worn wear 14. The collapsed
fine-tune controls and reset operate per look, with preferences remembered
across restarts. This tool intentionally retains its independent development
controls and CLI defaults. Opening it does not change the app's preferences.

## Build and open

From the repository root:

```sh
cmake -S tools/artwork-preview -B build/artwork-preview -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/artwork-preview -j 4
build/artwork-preview/zuuned-artwork-preview
```

The window compares **Original / Clean ink / Halftone / Worn print**. Choose an
artwork tab, move **Detail** (higher retains more tones/fine detail) and **Texture**
(affects halftone and worn print), or select an image through **your computer**.
Artist samples also show 48px and 80px circular thumbnails. Palette swatches come
from that source image; grayscale inputs remain neutral.
Short windows scroll vertically so the extra controls do not shrink the artwork.

The **Halftone** row has **color / black & white** ink choices and a separate
**Dot size** slider from fine to comic. Dot size controls spacing/diameter;
Texture controls print strength. Color uses rotated cyan/magenta/yellow/black
screens; monochrome uses black ink on white stock. Comic contrast expands the
source's usable tone range before screening, keeping a dim portrait readable.
Texture zero restores exact Clean ink, including its original colors. These new
controls affect only Halftone; Clean ink and the approved Worn treatment keep
their existing output and cache entries.

The default discovery reads `Zuuned/Zuuned` below Qt's generic cache/data roots.
It looks for the cached Billie Eilish, Christopher Larkin and Adele portraits,
Skyfall cover, and 1917/Alien posters. A read-only SQLite connection supplies
poster paths/titles and optional album examples. Other cached files are used if
those examples or the library are unavailable. No scan or provider lookup runs.

Override locations when needed:

```sh
build/artwork-preview/zuuned-artwork-preview \
  --cache-root /path/to/Zuuned/Zuuned \
  --library-db /path/to/library.db \
  --output /tmp/zuuned-artwork-preview
```

All derived images live under the output directory, which defaults to
`/tmp/zuuned-artwork-preview`. The input cache and library are read-only. Input
decoding is capped at 32 MB encoded / 64 million pixels and previews at an 800px
long edge. Source bytes are hashed and decoded from the same snapshot; cache keys
include renderer revision, detail and texture. Increment `rendererRevision` when
changing output behavior or render-size policy.
Only Halftone's variant key adds dot size and monochrome; the source palette and
other three variants are reused when those two controls change.

Only one render batch runs at a time. Changes debounce, obsolete results are
discarded, and the latest source/settings win. Detail/texture changes retain the
current display while preparing; choosing a different source clears the old
results so failures cannot mislabel another image. Reselecting the same file
refreshes changed/repaired content. Owned cache entries are bounded to 160 PNGs,
with orphan palette records removed; unrelated files and the active input are
excluded from pruning.

## Export actual comparisons

```sh
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
  build/artwork-preview/zuuned-artwork-preview --export
```

This produces one comparison sheet per cached sample and `manifest.json` with
source paths, generated variants, palettes and timings. The defaults are detail
55 / texture 35. Use `--detail`, `--texture`, and `--sample` to reproduce a
particular treatment, for example:

```sh
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
  build/artwork-preview/zuuned-artwork-preview --export \
  --sample Alien --detail 55 --texture 100 --output /tmp/zuuned-worn-max
```

These options also set the starting controls/sample in the live window.
Halftone also accepts `--dot-size` (0–100, default 35) and `--monochrome`:

```sh
build/artwork-preview/zuuned-artwork-preview --sample "Billie Eilish" \
  --detail 39 --texture 100 --dot-size 30 --monochrome
```

Add `--export` and an output directory for a reproducible comparison. Export
captions and manifest records include all controls and the renderer revision.
Timings are per-variant render plus PNG encoding/write on a cold
cache, and cache-hit checks on a warm cache; they exclude shared input decoding.
They are not end-to-end frame-rate measurements. Outputs containing real cached
artwork are runtime review files, not repository assets.

To capture the actual live window after all four images load:

```sh
build/artwork-preview/zuuned-artwork-preview \
  --capture /tmp/zuuned-artwork-preview/live-window.png
```

The window remains open. Add `--exit-after-capture` for a bounded GUI smoke check.
This closes only the standalone preview, never the running ZUUNED application.

## Validation — 2026-09-08

- `bash tests/artwork-print/run.sh`: 34 isolated renderer checks passed, including
  all three treatments, source immutability, deterministic concurrent calls,
  neutral grayscale, detail/edge behavior, empty/tiny images, control limits,
  premultiplied input and exact integer/float alpha preservation. Separate checks
  verify image-specific wear on unchanged regions of equal-palette sources,
  repeatability, stable placement across Detail/Texture changes, connected surface
  marks beyond the rim and restrained central contrast. Edge chips survive
  reduction to 280px cards and 64px thumbnails; increasing Texture increases wear.
  Halftone checks cover increasing dot diameter/spacing, neutral monochrome and
  alpha, flat-swatch optical averages, control bounds/nonfinite input, exact
  zero-texture bypass, and unchanged other styles under Halftone-only controls.
  All 34 renderer checks also passed under AddressSanitizer after the surface
  correction, including an exact noise-endpoint input in tiny-image coverage.
- `bash tests/artwork-print/run-preview.sh`: 19 actual-controller checks passed,
  including empty-state sliders, same-path replacement/repair, stale-worker
  suppression, cache reuse and safe pruning without altering input images.
  New checks cover dot/mode updates during active rendering, only-Halftone cache
  invalidation, restored-setting reuse and the other variants remaining intact.
- Release build and actual export passed on all six named cached samples.
  The initial cold exports measured 26–157 ms per treatment on this machine at
  up to 800px, including PNG writes. Real faces, small thumbnails and Skyfall's
  lettering were visually inspected. The live Qt window was opened and captured.
  Comic color/monochrome comparisons were checked on the cached portrait at
  Detail 39 / Texture 100 / Dot size 30. Full-strength color intentionally shows
  strong process-ink separation; lower Texture softens it. The dot treatment
  reproduces print behavior rather than drawing new comic line art.

This proof establishes real local processing and interactive comparison. The
main app now adds its own bounded worker/cache lifecycle and shared image
surfaces; its native build, worker/cache and OpenGL display gates, existing
regressions and actual Appearance capture pass. See the linked study for exact
coverage and commands. The palette simplification preserves composition; it does not
redraw faces or reproduce every detail of the generated concept sheet.

**Physical reference, 2026-09-08:** the user supplied a handled VHS cardboard
sleeve. The target combines chipped edges/corners, rubbed coating across the
surface, and sparse connected scratches/creases. A chipped border alone is
insufficient. Dense speckles across faces and repeating directional marks remain
the wrong approach. Each artwork must have its own damage pattern.

Worn starts from Clean ink. A hash of the original decoded pixels seeds variable
rub locations, sizes and angles, broken scratch paths, one bent handling crease,
and edge chip fields. The seed excludes Detail and Texture: controls deepen the
existing wear rather than moving it. Reopening identical content reproduces the
same result, and replacement image content gets a different pattern. No clock,
shared random generator, network texture, or external processing is involved.

Rubbed color loses saturation and ink density. Sparse coating scuffs and creases
reveal muted source-derived stock, strongest near handled edges. Surface wear
can now cross the center; its contrast is bounded so it remains subordinate to
faces and lettering. Alpha is preserved. Texture zero restores exact Clean ink.
Mask allocation failures propagate as an empty renderer result before access.

**Surface correction, 2026-09-08:** square-looking scuff clusters were traced to
thresholded square-cell value noise. Surface abrasion now uses warped triangular
gradient noise at several scales, mixed with strokes following each rub's
direction. The resulting coating loss is irregular and softly faded. Edge chips,
content-based repeatability, alpha preservation and restrained center contrast
retain their existing contracts. Shape is checked visually against the user's
portrait and VHS reference; the automated checks do not establish material realism.

All six actual cached samples export at Detail 39 / Texture 35 and 100. Billie,
Skyfall and Alien were visually checked for differing wear, clear faces, readable
lettering and retained source colors. At maximum strength some scuff clusters
are more stippled and the exposed paper brighter than the physical reference;
these remain visual taste decisions for user review. Circular crops retain
surface rubs/scratches but still hide much of the rectangular edge wear. Production
use must share source identity across decode-size buckets and consider
the print shape. This standalone preview remains a separate development tool;
the main-app Appearance integration is documented above.
The renderer revision prevents old treatments from being reused from cache.
