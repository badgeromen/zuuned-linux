# HD artist artwork preparation

Run `bash tests/hd-artist-art/run.sh` for an isolated native Qt Core/Gui fixture.
It compiles the production `HdArtistArtStore.cpp`, never opens a real library or
USB device, and checks immutable original/derivative copies, role dimensions,
source deletion after Apply, stable identity on unchanged images, stale drafts,
invalid/duplicate/oversized inputs, checksum validation, local reset, and extreme
aspect-ratio inputs that previously risked huge scaling intermediates.

The actual Customize QML stage/cancel/shared Apply checks live in
`tests/customize/tst_CustomizeSheet.qml`; run `bash tests/customize/run.sh`.
The actual asynchronous service save, portrait preservation, stale Apply refusal,
and artist rename/match migration live in `tests/customize/backend.cpp`; run
`node tests/customize/run-backend.mjs` after building the application.

These are software tests. They do not certify firmware refresh of previously
cached artists, on-device presentation, or multi-artist hardware behavior.
