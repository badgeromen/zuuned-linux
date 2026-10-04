# Video display title and object filename regressions

After building the app, run `node tests/video-titles/run.mjs` from the repository
root. An optional argument selects another existing build directory. The runner
reuses the app's compiler flags and object files, links a disposable native
fixture under `/tmp`, and isolates all XDG directories. It does not rebuild or
launch ZUUNED, open a library, instantiate DeviceService, or access USB.

The checks exercise production display-title, transfer-filename, import-filename,
on-device episode-key, and interrupted-transfer matching helpers. They cover
punctuation, known extensions, unsafe filename characters, missing titles, and
identical episode titles across different series/seasons/episodes. The compiled
SyncEngine signal and DeviceWorker slot must carry separate filename and title
parameters in the same order.

Startup checks instantiate an unattached SyncEngine against disposable marker
and queue files. They cover typed and legacy interrupted-send markers, retaining
recovery evidence while offline, and persisting/restoring upload warnings even
when the original source file is unavailable. Those warnings remain outside the
pending/failed transfer estimate and survive completed-row cleanup.

The fixture checks the native signal contract; it does not enter the private
transfer pipeline or claim to verify wire metadata on hardware. The companion
`bash tests/actions/video-run.sh` regression checks that background scanning does
not move or resize the production gallery.
