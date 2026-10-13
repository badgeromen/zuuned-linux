# Podcasts and Audiobooks visual study

A private, interactive design proposal. The user approved the initial visual
direction and requested custom covers and preserved drag interactions. Work stays on `feature/podcasts-audiobooks`.
Open `index.html` directly, or run:

```sh
node mockups/spoken-media/serve.mjs
```

Then visit http://127.0.0.1:8341. Node built-ins only; no install or build step.
This is a browser proof of the visual design, not a webview proposed for the app.
Production implementation remains native Qt/QML and worker-backed C++ services.

## Proposed organization

Podcasts and Audiobooks get separate top-level library navigation entries.
Both retain the existing side-panel composition, pink/orange identity and fonts.

- Podcasts: shows, unplayed, downloaded; a continue-listening strip; square show
  covers; show details with episode selection, separate local download and Zune
  actions. The RSS dialog previews identity/art before subscription.
- Audiobooks: all books, in progress, not started, finished; one cover per book;
  detail tabs for chapters, bookmarks and source files; author and narrator are
  distinct. Multiple source files should group under one reviewed book identity.
- Spoken playback: resume, skip back 15 seconds/forward 30 seconds, speed, sleep
  timer and bookmarks. The footer demonstrates controls to integrate into the
  existing player instead of introducing a second audio engine.
- Native import: standard desktop file/folder picker, then a review of book
  identity, cover, parts and ordering. The prototype starts at that review.
- Device: the existing queue accepts explicit episode selection. Already-on-device
  items are skipped; connection is required when adding, and disconnect leaves
  queued items intact. Book transfer remains disabled until its hardware gate.

## Try it

Click Moonbound, select episodes, add to the preview queue, then repeat the action.
The trailer is already marked on Zune and is excluded. Toggle the connection
checkbox in the study bar to see the add boundary and preserved queue.
Preview downloads still work offline from the device.

Try Add podcast, the supplied RSS URL, a malformed URL, and Escape. Switch to
Audiobooks, filter by progress, open a book and switch chapters/bookmarks/files.
Add books opens the sample import review. All operations use memory only and
reset on reload. Buttons for playback, refresh, bookmarks and sleep are visual
previews and say so; they do not run those services. Other fictional podcast
covers are display samples; Moonbound is the complete show walkthrough.

## Images and provenance

`previews/` contains seven rendered browser captures:

1. `01-podcasts.png`: show library.
2. `02-podcast-detail.png`: Moonbound episodes and bulk actions.
3. `03-audiobooks.png`: book library and progress.
4. `04-audiobook-detail.png`: chapters and resume.
5. `05-add-podcast.png`: RSS subscription preview.
6. `06-import-book.png`: book import review.
7. `07-narrow.png`: narrow show-detail layout.

Moonbound's original JPEG was extracted without re-encoding from the user-selected
trailer MP3, because the feed's external image URL returned HTTP 403 to the test
request. Its feed/title/author and displayed episode titles come from the supplied
RSS. Listening/download states are fictional design fixtures except the actual
retained trailer transfer. The 29-episode count comes from the parsed feed; the
prototype shows a subset. Show description copy is a design placeholder.

Other covers are original local SVG typography/geometric illustrations created
for this study. Their books, shows, people and chapter names are fictional.
No image generation service was used. Existing bundled Specimen Alien and
Permanent Marker fonts are referenced from `qml/fonts/`. CSS mirrors `Theme.qml`;
the sidebar texture approximates the native grunge and does not replace it.

## Validation and remaining design decisions

Chromium captures checked at 1600×1080 and 820×1000. All four routes checked for
horizontal overflow at 640×900. Interaction checks passed for selection, queue
dedup, connection gating, disconnect queue preservation, simulated downloads,
filters, detail tabs, RSS validation and Escape. No JavaScript errors recorded.
These are prototype checks, not application or hardware gates.

Review the sidebar placement, cover sizes, continue-listening strip, episode-row
information density and shared spoken controls before native implementation.
The production player should use the existing responsive now-playing surfaces;
the preview footer is a control study, not a request to replace all player chrome.
Future native import review needs real ordered file selection and staged artwork
editing. Chapters/bookmarks in this first visual pass are illustrative samples.

## Custom covers and dragging, second pass

Right-click a show/book cover or use its Customize button. Choose/drop a local
JPEG, PNG or WebP in the staged editor. Apply updates the item’s shelf/detail and
player art; Cancel, close and Escape leave it alone. Reset to automatic is also
staged. Editing works without a connected device. These overrides are browser
memory only, cleared by reload; production must persist them by stable item ID.
The prototype limits uploads to 10 MiB / 40 megapixels, validates image decoding,
rejects other formats, and invalidates stale image loads on replacement/close.

Drag an episode row or a selected group to the device pillar. A count appears on
the drag indicator and drop target. The same feed-only/downloaded state and
already-on-device/queued exclusions apply as for the add button. Disconnection
between drag start and drop rejects the addition and preserves the queue. The
button remains the accessible alternative when the pillar collapses.

A book should drag as one ordered book, not separate anonymous chapters; that
native transfer path is still gated. A show-level device drag should open episode
selection rather than silently download/queue its whole history. These two drag
interactions are design requirements, not implemented gestures in this preview.

Additional images: `08-custom-cover.png`, `09-selected-episode-queue.png`,
`10-drag-target.png`, `11-narrow-custom-cover.png`.

Second-pass checks exercised actual browser drag-and-drop for image files and
selected episode rows, staged Apply/Cancel/Escape/reset, invalid images, isolation
between books, shelf/detail/player propagation, preservation across simulated
feed refresh, duplicate suppression, mid-drag disconnect and offline/narrow cover
editing. All passed with no JavaScript errors. Nothing touched USB or the library.


## Native implementation correction

The user's final control decision supersedes visible Customize buttons in this
historical study: Customize belongs only in the right-click menu. Covers use the
existing + / arrow control at the top, with episode selection under the arrow.
The native `SpokenMediaPage`/`SpokenCoverSheet` implement that placement. The native
first increment supports local imports, downloads, artwork and playback; device
buttons/media drags remain disabled until the typed queue and recovery path are
implemented. The browser's simulated device queue does not certify USB support.
