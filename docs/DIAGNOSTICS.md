# Local logs and tester reports

ZUUNED records native stderr and Qt diagnostics automatically, including startup
before the window opens. On Linux, logs live under
`$XDG_STATE_HOME/Zuuned/Zuuned/logs` (normally
`~/.local/state/Zuuned/Zuuned/logs`). Settings → Health → **open logs** opens the
actual directory chosen by Qt. `--version` exits before logging or profile setup.

Each completed line receives a UTC timestamp. Up to eight files of 2 MiB each
are retained across sessions; long individual lines are bounded. Directories are
owner-only and log files use mode `0600`. Logger startup/disk errors do not stop
the app; Health reports when recording is unavailable.

## Collecting a report

1. Reproduce the problem. If the app exited, reopen it; recent sessions remain.
2. Open Settings → Health → **export debug report** and choose a `.txt` file.
3. Use **open report** to review the saved file, then attach it with reproduction
   steps, expected behavior and a screenshot if the issue is visual.

The report contains exact app/libzune revisions, Qt build/runtime versions,
distro/kernel/architecture, display sizes/scales, graphics PCI IDs and driver,
Zune presence/model and effective USB-node read/write access, custom-rule state,
library counts and transfer activity. Hardware inspection reads sysfs and access
permissions only; exporting never opens a libzune session or probes the device.
It also includes up to 4 MiB of recent session logs. Exports are capped at 6 MiB,
written atomically with owner-only permissions, and report actual completion.

Reports do not contain the library database, full environment/settings, media
files, machine IDs or a crash core. Credential/home-path/device-identifier filters
run before logs reach disk and again at export; authentication payload dumps are
omitted. This is not complete anonymization: media names, mount paths, network
hosts and unfamiliar text may remain. Reports stay local and are never uploaded
automatically. Review them before sharing.

When the app cannot launch at all, a loader/FUSE failure may occur before native
logging can start. Launch the exact AppImage from a terminal with
`2>&1 | tee zuuned-launch.log`, as shown in
[TESTER_INSTALL.md](../packaging/TESTER_INSTALL.md). Previously written session
logs remain available in the state directory. Native crashes can lose the final
queued or unfinished line; a missing session-end marker is not proof of a crash.

## Implementation and verification

- `SessionLog` owns process-level stderr/Qt capture, private bounded files,
  previous-session snapshots and best-effort console mirroring. See
  [the core contract](../tests/diagnostics/CORE.md) for lifecycle/concurrency and
  redaction limits. File reads/filtering during export happen outside the logger
  mutex, so an export does not hold up the capture thread for a multi-MiB read.
- `DiagnosticsService` captures reactive app state on the UI thread, collects
  allowlisted system information and writes the report on a worker. The save
  dialog/controls are `DiagnosticsSettings.qml`, embedded in Health.
- Native verification: 72 logger/redaction checks, also passing ASan/UBSan, and
  34 report/export/QML checks. The latter cover native async success/failure,
  settings-secret removal, owner permissions, protected paths, special-file
  refusal, size limits and the rendered production Health controls.
- The clean-container harness checks actual packaged logs and performs the
  Health export through its real save dialog. A harness change alone is not a
  runtime pass; record the actual candidate result in `TESTER_RELEASE.md`.
