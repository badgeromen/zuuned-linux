# Zuuned Sans

An original, lightweight humanist sans for Zuuned's third **display font** choice.
The goal is the calm, open feel of the old Zune interface, with original drawings.
This is not Segoe, a traced font, or a converted third-party typeface. The generator
reads no other font file. Existing Permanent Marker and Specimen Alien choices
are preserved, and Permanent Marker remains the default. Body text is unchanged.

The real installable font is `output/ZuunedSans-Regular.ttf`, copied to
`qml/fonts/ZuunedSans-Regular.ttf` for application resources. Settings → Appearance
and onboarding offer **Zuuned Sans**, persisted as `headerFont=zuunedSans`.
`output/specimen.png` renders the actual TTF, including real lowercase.

## Declared coverage

350 mapped Unicode characters / 351 glyphs: printable ASCII, complete Latin-1
Supplement and Latin Extended-A, thirteen combining accents, capital sharp S,
and selected typography/currency marks. The exact manifest is
`output/coverage.json`. Lowercase has distinct outlines, including ascenders and
descenders. Numbers, punctuation, ligatures and accented Latin forms are included.

This is one unhinted Regular display face, with no italics or weight family. Greek (apart from the Latin-1 micro sign), Cyrillic, Arabic and CJK are
outside its scope and use the application's normal fallback. It is not a claim
to cover all Unicode. Unicode names in the manifest identify coverage, not a
promise of native-language typographic review for every character.

## Source, build and verification

`build_font.py` authors centerline Bézier curves and geometric contours, creates
accented forms from these original outlines, and emits deterministic TrueType
contours, original kerning pairs and mark anchors. It does not load, trace or
sample any existing typeface. Developer dependencies are Python, PySide6 and
fontTools, plus Pillow for the preview. None are application runtime dependencies.

```sh
QT_QPA_PLATFORM=offscreen python tools/font-studio/variants/zuuned-sans/build_font.py
python tools/font-studio/variants/zuuned-sans/validate_font.py
python tools/font-studio/variants/zuuned-sans/preview_font.py
cp tools/font-studio/variants/zuuned-sans/output/ZuunedSans-Regular.ttf qml/fonts/
ARTWORK_TEST_INPUT=tests/artwork-display/qml/tst_DisplayFont.qml \
  bash tests/artwork-display/run.sh DisplayFont::test_bundledAlien
bash tests/artwork-print/run-settings.sh
```

Validation checks the entire declared cmap, contours, advances, vertical bounds,
true lowercase outlines, Qt family loading, representative multilingual shaping,
combining marks, and precomposed/decomposed accent advances. The production QML
test covers all three bundled families and legacy preference migration. The
settings test checks the new preference survives a fresh process. These isolated
tests do not restart the user's application or access USB.

## Distribution

The generated font is **SIL Open Font License 1.1**, with no Reserved Font Names.
See [OFL.txt](OFL.txt), also shipped as `qml/fonts/ZuunedSans-OFL.txt`, embedded in
the app resources and installed with application notices. Font name-table entries
13 and 14 identify the OFL and its official text. The generator code is original
project code under GPL-3.0-or-later; the font output carries the separate OFL grant.
Existing font licenses are unchanged. This records provenance and distribution
terms, not a guarantee about legal protection or a claim of affiliation with
Microsoft.
