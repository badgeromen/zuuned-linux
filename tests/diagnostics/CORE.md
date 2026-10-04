# Native diagnostic logging

Run `bash tests/diagnostics/run-core.sh`. This builds a Qt Core-only binary in
a disposable directory; it does not open the application, a library database,
or USB. Tests cover C `fprintf`, direct fd2 writes, Qt warnings/fatal messages,
four concurrent writers, incomplete and oversized lines, private permissions,
rotation, previous sessions, disk/startup failures, symlink rejection, stdout,
console mirroring, and cross-process capture ownership. The fatal subprocess
intercepts SIGABRT after Qt's fatal path so it creates no core dump or desktop
crash notification.

## Contract

`SessionLog::start(directory)` must run after the harmless `--version` early
exit and before `QGuiApplication`. Set `RedactionContext` before starting it,
and refresh known secrets before export. Stop after the engine, application
services and GUI application have been destroyed. The singleton is a plain
C++ object and requires no event loop. It installs a Qt message handler and
redirects native stderr into a dedicated reader thread; stdout is untouched.
It restores the original Qt handler/stderr on shutdown and startup failures
leave the application able to run. No libzune callbacks or USB calls are added.

The supplied directory is owner-only (0700); files are created atomically with
0600, exclusive creation and no symlink following. A nonblocking advisory lock
on the directory prevents another application invocation from deleting the
active process's evidence before the normal single-instance check runs.
Only owned regular `zuuned-*.log` files are included in snapshots/pruning.

Files use a UTC session timestamp, process ID, random suffix and part number.
Default retention is **8 files, at most 2 MiB each**, across sessions, with the
oldest removed on creation/rotation. Each completed stderr line is sanitized,
timestamped in UTC and written immediately with unbuffered system calls. Raw
line accumulation is bounded at 16 KiB; the remaining oversized line is dropped
through its next newline. Carriage-return progress is treated as lines. The
original console receives a best-effort, sanitized copy through an independent
nonblocking descriptor, so a blocked terminal does not hold up the log reader.

The capture pipe itself preserves blocking writer semantics. Severe filesystem
stalls or overwhelming output can still backpressure a writer; logging cannot
promise real-time behavior when the underlying filesystem stops responding.
After a disk error, capture keeps draining and mirroring, recording becomes
unavailable, and `error()` describes the failure. No runaway retry loop occurs.

`snapshot()` requests a bounded drain cut, then pins a small set of file
descriptors and sizes under the reader mutex. File reads and whole-report
sanitization run **outside that mutex**. Rotation or stop during export cannot
invalidate pinned evidence; exports do not stall stderr while reading MiBs.
The default snapshot includes up to 4 MiB of newest current/previous records,
in chronological order, plus a sanitized copy of a pending incomplete line.
It never removes the pending line, avoiding credential fragments at the next
write. Snapshot limits are capped at 8 MiB. Partial leading records are omitted.

Qt fatal messages synchronously request a drain before Qt aborts. Normal stop
drains queued data and includes any unfinished line. Native crashes/SIGKILL can
lose the last still-queued bytes or an unterminated line. Files already written
survive an application crash; this is not a core dump, signal handler, durable
journal, or a guarantee against power loss. A missing `session ended` marker
is evidence of an interrupted session, not proof of a crash.

## Privacy limits

Filtering runs **before persistence and again on export**. It removes supplied
literal/URL-encoded secrets, URL user-info/query values/fragments, labeled API
keys/tokens/passwords/authorization/cookies/serials/user names, conventional
home-directory account names, configured home/user strings, and personal names
in libzune's connected-device line. Existing MTPZ handshake payload lines,
hex-dump rows/fragments and long hex blobs are excluded while response codes and
ordinary labeled build revisions remain. Multiline PEM private-key/certificate
blocks are suppressed statefully until their end marker, before disk writes.

This is a conservative text filter, **not a general anonymizer**. Media titles,
file names outside the replaced home prefix, network hostnames, mount paths,
and other text useful for diagnosing imports/playback may remain. Unknown
unlabeled credentials or arbitrarily interleaved output from multiple native
writers cannot be perfectly recognized. Reports must stay local and reviewable;
never silently upload them, include full settings/environment dumps, or promise
that a report contains no personal information. A secret added to the redaction
context later protects future lines and exports; it does not rewrite prior
files. File ownership/permissions do not defend against another process running
as the same user deliberately manipulating that user's diagnostic directory.
