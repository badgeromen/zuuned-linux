# Customize design studies

Created on `ux/6-customize-design`, branched from `ux/6-customize` at
`b42fb9e`. This is a local, interactive visual study for review before
porting an agreed direction into the native QML application.

Open **index.html** in a browser. It works directly from disk, with no build,
package install, or network. Optionally run `node mockups/customize/serve.mjs`
from the repository root and visit http://127.0.0.1:8338.

The prototype reuses the bundled Permanent Marker and Specimen Alien fonts and
mirrors the palette in `qml/Theme.qml`. No production app, library, device,
settings, CMake, or libzune code is changed.

## Directions

- **Sleeve:** big, slightly tilted art, a vinyl edge, restrained glass, and
  editable liner notes. Closest to the current application composition.
- **After hours:** the art bleeds into the background of the floating editor;
  controls remain on a dark, readable surface.
- **Bootleg:** taped artwork, warm paper edges, offset framing, stronger
  marker typography, and an orange Apply button. The working area stays clean.

All directions can be tried with an album, artist, movie, or manually
identified series. Shape follows the item: square sleeve, circular portrait,
or portrait poster. The font switch previews the existing graffiti face.

## Interaction scope

- Drafts survive direction, item, mode, and tab changes.
- Editing fields and selecting art update the live preview.
- Apply saves **in-memory demo state only**. Reopen to see it.
- Cancel, close, and Escape discard that item's unapplied edits.
- Reset to auto stages an automatic baseline; Apply commits the reset.
- Identity matching preserves selected art; artwork preserves manual identity.
- Local image upload and drop work; JPEG, PNG, and WebP are accepted.
- Source tabs and searches use **fictional sample results**, not live APIs.
  Frame grabs are composition examples. No real media is decoded.
- Refreshing the browser resets the demonstration. No real collection data is
  ever read or written.

This first visual pass focuses on the four requested item types. Genre and
mixtape artwork, persistent production overrides, and actual provider queries
remain part of the native implementation scope after design review.

## Review artifacts

`previews/` contains browser captures of the three directions, identity
editing, a manual series, and a narrow layout. The preview URL hash accepts
`direction=sleeve|afterhours|bootleg`, `kind=album|artist|movie|series`, and
`tab=identity|artwork` to open a specific study on page load.

Sample artwork was generated with the built-in imagegen tool. The exact
prompts are in `assets/PROMPTS.md`; all three assets are stored locally.

Browser validation passed for draft isolation, Apply/Cancel/reset, independent
identity and artwork, the manual-series reset, footer placement, and 24 layout
combinations (three directions, four item types, desktop and narrow widths).
No JavaScript exceptions were recorded during those checks.

Review before a QML port: preferred direction, amount of grunge, size and
placement of the art, quieter text versus marker accents, and source navigation.
