# ZUUNED artwork print study

[Open the comparison sheet](01-print-study.png).

`01-print-study.png` is a concept comparison generated with the built-in
imagegen tool on 2026-09-07. Left to right: original, clean ink, halftone,
worn print. The original column label was omitted by the generator; the source
art is the first image in each row. Images and circles are illustrative, not
pixel-exact app layouts or output from an implemented filter.

The original fictional inputs are `../customize/assets/artist.png`,
`../customize/assets/after-the-last-train.png`, and
`../customize/assets/series.png`. See their adjacent `PROMPTS.md` for provenance.
The first attempt using a cached real-artist portrait was rejected by the image
generator; the delivered sheet uses only these fictional sample assets.

Read [the design and implementation discussion](../../docs/ARTWORK_STYLE_STUDY.md).
The production app has no new artwork setting in this delivery. A deterministic
local implementation still needs to establish what quality and speed are feasible
on real covers, especially lettering, subtle faces and small thumbnails.

## Final prompt

```text
Use case: style-transfer.
Create one high-resolution comparative artwork concept sheet for ZUUNED, an independent music/media player with Zune-era screen-print/zine design language. The three attached edit targets are original fictional artwork previously generated for this app: 1 a fictional musician portrait, 2 a fictional album cover of a figure on a city street below an orange sun, 3 a fictional sci-fi poster of a rooftop traveler below a blue fragmented moon. Preserve their composition and recognizable subjects across all treatments. These are not real people or published media titles.

Wide dark charcoal sheet, approximately 2400 by 2000 pixels, four precisely aligned columns and three rows of art, substantial breathing room, understated light-gray sans serif. Header small tracked "ZUUNED / ARTWORK STUDY 01" then lively hand-painted marker headline "YOUR ART. YOUR INK." in orange-to-pink gradient. Small subtitle "Concept treatments — original palette". Column labels, exactly: "original", "clean ink", "halftone", "worn print". Row labels exactly "artist", "album", "series". Do not create an app screenshot or settings screen. No buttons, boxes, emojis, logo symbols or decorative rules above the title.

Each row uses the same reference artwork four times, identically framed, arranged under the four labels. First two rows square; bottom series images tall2:3, with the same width as other rows. Original column reproduces the source faithfully. Clean ink column converts that source to bold but recognizable 4-5-tone screen-printed pop art with its dominant colors. Preserve facial structure, album's orange sun/magenta sky and street silhouette, and series's blue/cyan moon and traveler. Reduce small texture and simplify big forms into flat coherent ink shapes. The artist must stay grayscale because its source is grayscale: don't invent a colorful palette. Halftone column uses the same simplified source palette with crisp but subtle regular halftone dots only in middle tones. Worn print column uses a stronger3-tone source palette and visible restrained photocopier grain, ink wear and slight ink misregistration. Make the three treatments visibly distinct and expressive; do not just tint the source photographs. Preserve faces and silhouettes. No outline on every tiny detail. No objects or type inserted into the artworks.

Under each row's label include a tiny source palette strip. The album should contrast richly with the cool blue series row, while the portrait stays clean black/gray/off-white. Add at the bottom a small-size check showing four64-pixel circles containing the portrait in each matching treatment aligned to the columns. Footer "Palette from your artwork. Texture to taste." with small "Fictional sample artwork • generated concept study". Avoid corporate UI, over-saturated rainbow palettes, speech bubbles, paper props, bevels, drop shadows or invented artwork. It should feel like an elegant independent art director's proof sheet, with the art doing the work.
```
