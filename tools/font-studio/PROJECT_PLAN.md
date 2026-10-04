# Font Studio — possible independent side project

Current deliverable is Specimen Alien's Latin display font, not a full editor.
No Zuuned UI/runtime changes or new desktop application are implied by this plan.

## How the current studio works

1. `build_font.py` authors letter skeletons, expands their strokes into vector
   outlines, cuts glyph-specific stencil bridges and adds seeded paint wear.
2. `completion.py` draws extra Latin forms, punctuation and symbols, composes
   accents and declares the supported Unicode character set.
3. fontTools writes real TrueType outlines, names, Unicode mapping, metrics,
   kerning and mark positioning. It does not borrow another font's outlines.
4. Preview scripts render the TTF. `validate_font.py` loads it in Qt and checks
   the exported font. `make_proof.py` supplies an offline editable-text proof.

Today, making another typeface means editing Python geometry and regenerating.
There is no mouse-driven outline editor, undo stack, project-file format or
import/edit workflow yet. Existing preview scripts are not a general font editor.
Developer dependencies stay separate from Zuuned's C++ runtime.

## Suggested standalone phases

- **1 — Creative workbench:** independent desktop project; font project files,
  glyph browser, live word preview, reusable stencil/spray controls, deterministic
  seeds, saved style variants, undo/redo and safe TTF export. A saved baseline
  must never be silently replaced by a variant.
- **2 — Real drawing tools:** nodes and Bézier handles, guides, boolean operations,
  spacing/kerning views, reusable components, accent anchors and Unicode coverage.
  Import fonts only with deliberate licensing/provenance tracking.
- **3 — Production tooling:** shaping tests, contour/metrics checks, supported
  script manifests, OpenType features, variable masters, OTF/WOFF2 exports,
  packaged standalone builds and recoverable project migrations.

First decision for a separate project: whether it focuses on procedural stencil
and graffiti lettering or aims to become a general-purpose type designer.
Do not couple the font editor to Zuuned or launch a broad rewrite as part of
shipping this font. A first workbench can reuse the proven geometry/export code.
