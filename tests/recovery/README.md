# Interrupted music/photo recovery

Run after a native build:

```sh
node tests/recovery/run-backend.mjs
```

The fixture constructs SyncEngine with disposable XDG state and links the real
native objects. It never constructs DeviceService, opens USB, or touches the
user's profile. Pure eligibility/readback checks accompany actual marker-file
and completion-correlation checks.

Music/photo markers are atomic JSON records containing media kind, intended
identity, device serial, pre-existing media IDs, a request token and returned
object ID when available. A failed send stops the batch and retains its marker.
New syncs cannot overwrite unresolved recovery. Only explicit dismissal or
confirmed, token-correlated exact deletion clears it.

Automatic recovery refuses legacy title-only markers, missing exact IDs, missing
or different serials, pre-existing IDs, invalid IDs and incompatible live media
readback. It does not infer a deletion target from unique-looking titles or
filenames. The worker rechecks the connected serial and exact object's metadata
before DeleteObject. Thus an ID-less crash requires manual review in the device
library, followed by explicit dismissal. The existing video filename recovery
path remains separate.

No hardware safety or transfer success is claimed by these fixtures. Real-device
recovery gates remain separate, controlled tests using disposable media.
