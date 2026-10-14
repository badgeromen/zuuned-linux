# Podcast cleanup worker gate

Build the application, then run:

```sh
node tests/podcast-cleanup-worker/run.mjs
```

The gate links the actual application objects and wraps every device function
used by the cleanup slots. Its synthetic handle never reaches libusb; no watcher
or facade is constructed. It cannot delete anything from a physical device.

Checks cover complete inspection, exact feed/enclosure mapping, marked-played
versus partial progress, nonzero bookmarks on played episodes, missing state,
keep-on-device exclusions, duplicate local/device identities, feed-scoped GUIDs, snapshot/live title and
duration agreement, metadata changes,
complete selection revalidation before deletion, cancellation/session/serial
guards and confirmed-only partial deletion results.

This verifies software policy and worker serialization boundaries. It does not
replace physical Classic/HD/Draco firmware-state or deletion acceptance testing.

The same wrapped gate also exercises spoken-browser enrichment: exact bulk
feed/enclosure membership, optional firmware played state, stale title/duration
rejection, incomplete-source failure, and correlated removal progress. Reads
remain write-free; progress marks an item removed only after a successful device
response.

Draco regression cases cover successful empty live inventory without any
listening-state request, unavailable nonempty state with no removal plan, failed
inventory and stale generations, and emptied inventory before deletion. The
browser retains exact podcast membership alongside Draco book inventory. Draco
book removal revalidates the complete identity and rejects changed metadata;
unsupported Scorpius inventory cannot authorize deletion. These are mocked
protocol contracts, not physical Draco playback or deletion acceptance.

The supported-family cleanup matrix repeats positive marked-played planning and
revalidated removal, Keep on Zune, failed initial state read, failed pre-delete
state read, and a fully unplayed no-op for Keel, Draco and Pavo. Scorpius remains
unsupported and never authorizes automatic deletion. Draco browser rows carry
played status only when exact state/live identity correlation succeeds.
