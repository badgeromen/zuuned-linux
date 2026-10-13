# Spoken library organization UI gate

Run `bash tests/spoken-organization/run-qml.sh` for the software-rendered gate.

For the actual OpenGL rendering path, including gradient titles:

```bash
PHOTO_TEST_BACKEND=rhi QSG_RHI_BACKEND=opengl bash tests/spoken-organization/run-qml.sh
```

The gate loads the production spoken pages, cards, library island and shared
gallery components with recording service singletons. Qt Quick Test performs
real pointer clicks against visible controls using disposable XDG directories.
It never opens the application profile, makes network requests or touches USB.

Covered contracts:

- Independent stars for audiobook books, podcast shows and individual episodes.
- Library favorites and all-show starred episode browsing update reactively.
- Actual pivot clicks followed by a media-kind switch restore a valid active tab
  and clear old detail/selection state.
- Local starring and library edits work without a connected device.
- Libraries preserve explicitly chosen reading order and navigation context.
- Edits remain drafts until Save; Cancel leaves stored membership untouched.
- Save sends the expected revision and waits for the matching completion.
- Stale failures preserve the draft and display the returned error.
- A book can be added to an existing collection without duplicate membership.
- Collection deletion requires confirmation and carries the original revision.
- Whole-book transfer identity and podcast episode-picker behavior are preserved.
- Device transfer actions and media drags stay disabled while disconnected.

Persistent schema migration, restart, refresh/reimport and optimistic concurrency
are exercised against the actual SQLite/native services in
`../spoken-media/native/run.sh`. These UI tests do not establish device behavior.

The suite also checks 500px and 600px page widths for toolbar overlap and gallery
bounds. It fails on QML binding loops, type errors, reference errors and undefined
assignments. The isolated fixture screenshots are written to
`/tmp/zuuned-spoken-shows-full.png`, `/tmp/zuuned-spoken-books-500.png`,
`/tmp/zuuned-spoken-shows-500.png` and
`/tmp/zuuned-book-library-island.png` (plus 600px variants).
These fixtures intentionally omit cover artwork. The software renderer does not
render shader-based gradient titles; use an OpenGL app review for final styling.

The native gesture gate begins card drags on exposed artwork, beside the shared
centered play affordance. This suite separately clicks that center button and
verifies playback without navigation or queue mutation.

Podcast picker checks cover newest/oldest count previews, manual selection,
selection survival through revisions, connection/sync guards, remembered preset
submission, cancel-without-saving, and Keep-on-Zune locking during sync. Native
service tests separately verify actual timestamp ordering and durable preferences.

The show-card and detail + actions delegate selection to the native service,
including feed-only episodes even while the Downloaded view is selected. The
picker uses the global episode count unless a show has a saved override.

Run `bash tests/spoken-organization/run-settings.sh` to verify actual native
settings defaults, count bounds, change notifications and fresh-process
persistence. Automatic queueing remains opt-in and independent of cleanup.

First-run setup coverage loads the production onboarding wizard and shared
podcast preference controls. It checks deferred settings writes, back/forward
navigation, abandonment, skip preservation, invalid count rejection, committing
an active custom-count field on completion, import success/failure handoff, and
800×600 step bounds. Setup fixture screenshots are saved to
`/tmp/zuuned-setup-4.png` (podcasts) and `/tmp/zuuned-setup-5.png` (audiobooks).
The import handoff uses recording signals; real folder probing and atomic saving
remain covered by the native spoken suite.

The BookLibrary cases replace the old modal editor tests. They exercise actual
offline cover drags into the floating island, reading-order handle drags and
arrow buttons, duplicate/type filtering, staged plus actions, dirty replacement
and cancellation, revision-bearing save/delete calls, stale errors and pending
write locks. OpenGL fixture previews of masked covers and the island are saved to
`/tmp/zuuned-rounded-book.png`, `/tmp/zuuned-rounded-podcast.png`,
`/tmp/zuuned-rounded-book-sleeve.png` and `/tmp/zuuned-book-library-art.png`.
No test connects to a real Zune or opens the user's profile.
