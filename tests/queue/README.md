# Transfer queue controls

```sh
PHOTO_TEST_INPUT=tests/queue bash tests/actions/photo-run.sh
PHOTO_TEST_INPUT=tests/queue/capture.qml bash tests/actions/photo-run.sh
```

Uses the production `QueueEntryView` with recording signals and an isolated
offscreen Qt window. No live queue, library, sync engine, or Zune is modified.

Pointer tests cover always-visible 32px remove controls, the corners of their
hit areas, stable text widths on hover, collapsed/expanded seasons, individual
entries, right-click alternatives, and a sync that starts while a menu is
open. The 188px content fixture matches a 220px panel with its normal insets.
A native QML list model checks the same required-role injection as the panel.
The capture command writes `/tmp/zuuned-queue-controls.png`, comparing native
queue controls in 220px and 300px panels.
