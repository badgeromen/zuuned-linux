# Debug report integration

Run `bash tests/diagnostics-report/run.sh`. The native Qt harness creates
disposable XDG settings, logs and export destinations, without opening a library
or a Zune session. Sysfs device/graphics inventory is read-only.

The 34 checks exercise production `SupportReport`, `DiagnosticsService` and the
Health diagnostics QML. They verify owner-only atomic exports; success/failure
signals and duplicate requests; credential/home/serial masking; build and app
state fields; refusal of nonlocal, missing-parent, directory, symlink, special
file, protected-directory and oversized destinations; and native QML rendering.
The screenshot is `/tmp/zuuned-diagnostics-health.png`.

These checks do not substitute for clicking the platform's actual save dialog.
That packaged-app interaction is covered separately by
`packaging/containers/smoke-runtime.sh` and its recorded candidate evidence.

Log capture/rotation/crash/redaction checks are independent in
`tests/diagnostics/run-core.sh`; see that directory's `CORE.md`.
