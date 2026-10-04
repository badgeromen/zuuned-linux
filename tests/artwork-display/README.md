# Native artwork display gate

This fixture compiles the production AppSettings, ArtworkRequest, worker/cache,
renderer, ArtworkImage and Appearance controls into an isolated Qt Quick module.
Generated input images and temporary config/cache/data roots prevent changes to
the user's library or preferences. It has no device or provider clients.

```sh
bash tests/artwork-display/run.sh
ARTWORK_TEST_PLATFORM=xcb ARTWORK_TEST_RENDERER=rhi bash tests/artwork-display/run.sh
```

The software run checks actual displayed pixels against the native renderer,
raw source URLs, Original returning during work, latest-slider publication,
photo opt-out and no raw-image flash while a replacement PNG loads. OpenGL also
checks hidden portrait/poster masks and composite movie artwork with overlays
through all three treatments and Original. Mask captures assert full dimensions
before pixel equality, so a clipped or empty test window cannot pass the gate.

The shared image keeps both underlying Image nodes present and switches opacity;
changing child visibility beneath an already-hidden mask source left a blank
texture on this Qt scene graph. The current print stays visible and leased while
its replacement renders/decodes. A new source immediately clears the old print.

Settings-specific interactions and cross-process preferences are covered by
`bash tests/artwork-print/run-settings.sh`; concurrency, cancellation, file safety
and pruning are covered by `bash tests/artwork-pipeline/run.sh`.

To capture the actual Appearance component with a local image:

```sh
ARTWORK_TEST_PLATFORM=xcb ARTWORK_TEST_RENDERER=rhi \
ARTWORK_TEST_INPUT=tests/artwork-display/capture.qml \
ARTWORK_CAPTURE_SOURCE=/absolute/path/to/artwork.jpg \
bash tests/artwork-display/run.sh
```

The image is read only. The capture is `/tmp/zuuned-artwork-appearance.png`;
it uses the production controls and the user's approved Halftone defaults.
