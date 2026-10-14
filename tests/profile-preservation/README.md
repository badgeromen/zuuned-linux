# Copied-profile customization preservation

Run `bash tests/profile-preservation/run.sh` with Bash and SQLite installed.
The fixture sources the actual runtime comparison helper without launching the
application, touching USB or opening a user profile.

The runtime gate preserves every original customization row ID/key, artwork path
and manual JSON field. Only `_artworkDiscovery` is auxiliary provider state.
New rows are accepted only for artist/album discovery objects without manual
fields or artwork. Other library tables retain their strict runtime comparisons.

The 23 fixtures include unchanged data, legitimate cache writes, equivalent JSON
formatting, manual identity/art/provider-pin/revision loss, key and row-ID changes,
array reordering, invalid additions and malformed JSON/schema. Each case also
checks unchanged database hashes and reports free of fixture private values.
Runtime evidence is bounded counts in `upgrade-customizations.txt`, not a copy
of the profile or its customization JSON. A real packaged runtime pass is still
required separately; these fixtures do not establish successful profile upgrade.
