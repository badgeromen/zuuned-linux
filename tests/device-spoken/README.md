# Device podcasts and audiobooks

`bash tests/device-spoken/run-qml.sh` exercises the production device page with
recording services and a disposable profile. It checks distinct same-name
collections, chapter order, real selection/confirmation mouse clicks, stale
selection removal, error state and disconnect/device-swap/sync deletion guards.
It never opens USB or modifies the user's library.

After building the app, `node tests/device-spoken/run-backend.mjs` links its actual
native objects and exercises DeviceService admission/completion with a disabled
USB watcher and disconnected worker requests. It covers complete inventory
publication, request/serial/session correlation, exact typed IDs, busy/sync
ownership, partial deletion and device-scoped artwork. The tests do not certify
physical deletion or playback.

`bash libzune/tests/run-spoken-inventory.sh` validates the C inventory APIs against
fixture transport under ASan/UBSan. The library owns schema and metadata reads;
Qt never implements wire protocols. All three gates are included in `ci/check.sh`.

Hardware acceptance uses the running application's Zune island. Browse Podcasts
and Audiobooks, open groups and inspect artwork/ordering. Physical deletion is a
separate explicit user action with a confirmation. Do not delete real content
merely to run these software gates.
