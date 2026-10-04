# Specimen — original font study

An original stencil display font drawn from geometric paths in `build_font.py`.
No existing font is read, traced, converted, or used as a base. Strokes, clipped
bowls, diagonal bridges, edge chips and surface wear are authored by the
project's generator. This is a reviewable first design, not a claim of a complete
international text font or exclusive legal protection.

The real installable font is `output/Specimen-Regular.ttf`; the rendered
specimen is `output/specimen.png`. The app and the existing tester AppImage are
unchanged. Review the design before replacing Misdemeanor and rebuilding.

## Coverage

- 134 glyphs, 160 mapped characters.
- Printable ASCII, numbers and punctuation; lowercase maps to capital forms.
- Common decomposable Latin-1 accents, typographic quotes, en/em dash, ellipsis,
  middle dot, bullet, pound and euro signs.
- Original spacing and a small set of OpenType kerning pairs.
- Heading use; body text and unsupported scripts need the normal UI fallback.

No third-party **font** license is inherited. The project owner has not yet
assigned distribution terms to this new asset. The font metadata records that
status. Existing app/dependency/font obligations are separate. Developer-tool
licenses are not a third-party outline source. The specimen uses system
Liberation Sans for explanatory labels only; it is not embedded in this font.

## Rebuild

Developer-only dependencies: Python, PySide6, fontTools, Pillow. These do not
become application/runtime dependencies. The preview uses the installed system
Liberation Sans font for labels.

```sh
QT_QPA_PLATFORM=offscreen python tools/font-studio/build_font.py
python tools/font-studio/preview_font.py
```

Seeded wear and fixed font timestamps make repeated generation stable within
the same toolchain. Edit the source centerlines, stroke width, bridge positions
or wear distribution to refine the design. `glyph()` determines contour nesting
before writing TrueType winding, preserving bowl and distress cutouts.

Verification: the generated TTF loads as `Zuuned Stencil` through Qt's
QFontDatabase/QRawFont; ASCII and representative accented/punctuation codepoints
resolve. All mapped non-space glyphs have outlines, advances are positive, and
outlines fit the vertical metrics. Rebuilding twice produced the same SHA256.
The actual TTF specimen was visually inspected at heading and 24 px sizes.

## Study 02 — sprayed stencil reference

The user's SETTINGS reference calls for a ragged paint rim, clumped missing
paint and detached spray islands. Version 0.200 replaces the uniform horizontal
cuts with local counter bridges, thickens the original strokes, roughens their
outlines and adds clustered distress plus detached ink contours. The I is a
narrow plain stem; spacing is tighter. No reference glyph was traced.

`python tools/font-studio/preview_settings.py` renders the actual TTF with a
9-degree tilt and orange-to-pink gradient into `output/settings-study02.png`.
It requires NumPy in addition to the developer dependencies above. Tilt and
color belong to this preview; the paint breakup and spray are in the TTF itself.
`python tools/font-studio/validate_font.py` verifies Qt loading and metrics.
The 0.200 font passed that check. The repeat-build checksum result above records
study 01; no repeat-build assertion is made here for study 02.

## Study 03 — alien street stencil

User direction: "alien Banksy"; original letterforms and geometry remain the
source. Version 0.300 gives every A–Z and 0–9 glyph an explicit bridge definition
with authored endpoints and width. These are structural stencil openings, not
random texture. Each bridge is checked to intersect its glyph. Spray flecks
are rejected when they overlap a bridge, keeping the cuts legible. Avoid
subtracting the identical bridge path again after wear/union: Qt Boolean
coincident-edge processing was observed to drop parts of S and G in that case.

Faceted asymmetric A/O/Q, a kinked N/H, offset T crown and raised M centre move
the forms toward the alien direction. Paint clusters are slightly reduced to
preserve the stencil read. `preview_glyphs.py` writes the full alphabet/digit
sheet `output/glyphs-study03.png`; `preview_settings.py` now writes
`output/settings-study03.png`. Both previews render the actual TTF. Qt loading,
coverage and metric checks passed; the glyph sheet and SETTINGS were inspected.
No app font selection or packaged executable was changed.


## Study 04 — Specimen / spray-can overspray

User-approved direction: retain study 03's readable alien stencil forms, add
more overspray, and name the font **Specimen**. Version 0.400 updates the actual
font family, full name, PostScript name, unique ID and filename. The previous
Zuuned Stencil TTF is a retained earlier study, not the current output.

Study 04 keeps the body wear and per-glyph stencil bridges while increasing
outside-edge sampling and the spray radius. Fine droplets dominate, with sparse
larger irregular paint blobs. Spray remains excluded from the stencil cuts.
Current previews: `settings-study04.png`, `glyphs-study04.png`, `specimen.png`.
Run `python tools/font-studio/validate_font.py` for the current font.

### Character completion after style approval

The current study still has 134 glyphs / 160 mapped characters. It is not a
finished character set. The user explicitly requires more than alphabet-only
coverage when the visual style is locked. Before app integration, inventory
actual UI/localization requirements and finish a documented character manifest:

- A–Z and a–z: decide whether lowercase stays deliberately capital-shaped or
  receives distinct forms; do not describe aliases as separately drawn glyphs.
- Digits, all printable ASCII punctuation, brackets, operators and symbols.
- Complete Latin-1 printable coverage, including characters currently missing
  such as Æ/æ, Ø/ø, ß and additional currency/math marks.
- Latin Extended-A and combining accents needed for supported Latin-script
  names, plus typography (quotes, dashes, ellipsis, bullets, NBSP).
- Explicit Unicode coverage and fallback policy for other scripts. “All glyphs”
  here means the agreed complete character set, not every Unicode character.
- Visual review of punctuation, digits, accented shapes and spacing at actual
  heading sizes; cmap/outline checks, Qt fallback checks and packaging tests.

The app and AppImage remain unchanged pending style approval and completion.

## Separate Specimen Alien study

`variants/specimen-alien/` is an independent source/output copy, with font family
`Specimen Alien` and PostScript name `SpecimenAlien-Regular`. Original Specimen
study 04's generator, TTF and approved SETTINGS preview were verified unchanged
by SHA256; the manifest is in the variant directory (paths relative to repo root).
Do not overwrite either design when refining the other.

The Alien variant reauthors 17 letter skeletons with oblique crowns, asymmetric
bowls, narrow waists and kinked stems, retaining separately placed stencil cuts
and outside-edge spray. Other glyphs currently retain Specimen forms; this is a
style study, not the completed all-glyph revision. The character completion
requirements above apply after the style decision.

Run the variant's `build_font.py`, `validate_font.py`, `preview_settings.py`,
`preview_glyphs.py` and `preview_comparison.py` directly. The comparison renders
both real TTFs. Qt loading/coverage/metric checks passed for the Alien variant;
no app integration or AppImage change has been made.

## Specimen Alien 1.000 — complete declared Latin display set

`variants/specimen-alien/output/SpecimenAlien-Regular.ttf` now covers **368
Unicode characters with 342 glyphs**: printable ASCII, the complete Latin-1
Supplement and Latin Extended-A ranges, capital sharp S, 13 combining accents
and selected typography, currency, math and directional symbols. Soft hyphen
is intentionally empty; spaces have no ink. Lowercase remains deliberately
capital-shaped (unicase), as in the approved design. Cyrillic, Greek alphabets,
Arabic, CJK and other scripts are outside this release's declared coverage;
this is not a claim to cover all Unicode.

`coverage.json` is the exact codepoint/name/glyph manifest. The supplied
`validate_font.py` requires all declared characters, tests actual Qt family
loading and fallback, outline/metric bounds and zero-advance combining marks.
It also compares precomposed/decomposed accented text and verifies mark layout
does not increase advance. Kerning and above/below mark anchors are in GPOS.
The full character set has five PNG proof sheets via `preview_coverage.py`.

`make_proof.py` writes `output/proof.html`: a self-contained offline font proof
with the real TTF embedded, editable text, size/tracking sliders, background
switch and full character grid. It is a preview, not an outline editor.
`PROJECT_PLAN.md` explains today's generator and a possible independent editor.
The previous 134-glyph study counts in earlier sections are historical.

Rebuild the Alien deliverable with:

```sh
QT_QPA_PLATFORM=offscreen python tools/font-studio/variants/specimen-alien/build_font.py
python tools/font-studio/variants/specimen-alien/validate_font.py
python tools/font-studio/variants/specimen-alien/preview_coverage.py
python tools/font-studio/variants/specimen-alien/make_proof.py
```

The owner still needs to choose distribution terms for this original font.
No Zuuned app integration, license replacement or GitHub upload is included in
this font-completion step. Original Specimen is preserved unchanged.

## Specimen Alien integration — October 1, 2026

The app now bundles `qml/fonts/SpecimenAlien-Regular.ttf` from the completed
Alien generator. Misdemeanor is removed from source resources, including the
preview tool, test targets and Customize mockup. Settings and onboarding offer
Specimen Alien. Existing `headerFont=misdemeanor` preferences resolve to
`specimenAlien`; future writes use the canonical name. Permanent Marker stays
the default. The original Specimen study is preserved.

Coverage is the declared 368-character Latin set, including digits, punctuation,
Latin accents, symbols and combining marks; it is not all Unicode. Generation
remains a developer tool with no new application runtime dependencies.
