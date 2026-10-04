# Artwork discovery service checks

Run `bash tests/artwork-discovery/run.sh` from the checkout. The fixture injects
provider results and image downloads and uses disposable caches. It does not
contact providers or a Zune.

Covers typed album and artist statuses, terminal result suppression, changed
input evidence, explicit retry, one bounded transient retry, and publishing
normalized image bytes before a ready result carries its provider identity.
General album covers remain offers and must not download automatically.

The existing `tests/music-identity/run.sh` suite additionally checks generation
invalidation, Customize pause, preservation of existing artwork, and destruction
while workers are active.
