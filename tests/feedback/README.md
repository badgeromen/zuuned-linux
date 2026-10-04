# Notification strip

Run `bash tests/feedback/run.sh` to exercise the production `ToastHost` with
native Qt Quick pointer events. The tests cover action/dismiss targets,
remaining-time pause on hover, immediate replacement, narrow/long text,
clicks outside the strip, action clicks above an open modal, and stable
placement when keyboard focus moves from the editor into the strip.

The runner isolates XDG configuration/data/cache paths and supplies only a
small settings fixture for the production theme. It never starts ZUUNED,
accesses its library, or contacts a device.

Create wide, long-message, and narrow native image captures with:

```sh
FEEDBACK_TEST_INPUT=tests/feedback/capture.qml bash tests/feedback/run.sh
```

The files are `/tmp/zuuned-feedback-wide.png`,
`/tmp/zuuned-feedback-long.png`, and `/tmp/zuuned-feedback-narrow.png`.
Their collection backdrop is a fixture; the notification is the actual
production component. No build or app restart is needed.
