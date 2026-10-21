# Zune HD artist content C API

The native C API serves artist backgrounds, a thumbnail, up to six gallery images
and an optional biography to a Zune HD through its authenticated USB connection.
It does not start a host web server, bind a socket, require Wi-Fi, or contact a
provider. Zuuned supplies prepared content and uses the same API as other clients.

The underlying research workflow delivered all eight image assets and a biography;
the user confirmed biography display on October 19. The extracted public API and
Zuuned's ordinary music-sync ordering have software coverage, but have not yet
repeated that physical acceptance test. Do not conflate these two evidence levels.
Existing-artist cache replacement and persistence across later sessions remain
separate gates. No API deletes/recreates artists to force a fetch.

## Public entry points

Include `zune.h`, link `libzune.a` or the shared library. The build additionally
links `libavcodec` for full native JPEG validation, alongside existing libavutil.
There is no new helper process at runtime. Darwin builds use pkg-config FFmpeg
headers/libraries with a Homebrew prefix fallback; this change was built on Linux,
not yet verified on macOS. Builds without `USE_LIBAV` reject JPEG
validation rather than accepting unchecked compressed data.

- `zune_hd_art_validate`: validates complete prepared content without USB.
- `zune_hd_art_serve`: blocking serialized catalog session, READY/progress callbacks
  and detailed delivery results.
- `zune_hd_art_ensure_artist`: safely reuses an exact existing binding or creates
  a fresh tagged artist after establishing absence.
- `zune_get_albums_checked`: complete, strict album identity/reference inventory
  for decisions that cannot safely use the legacy best-effort browser listing.

The complete declarations and ownership contracts are in
[the public header](../include/zune.h) and [API reference](API_REFERENCE.md).

## Prepare and persist content

Set `ZuneHDArtConfig.version = ZUNE_HD_ART_API_VERSION`. Supply:

| Field | Contract |
|---|---|
| Artist | Nonempty valid UTF-8, at most 256 bytes. |
| Catalog GUID | Stable, nonzero UUID retained across retries and revisions. |
| Background | JPEG, ID is catalog UUID with its final hexadecimal nibble XOR 1. |
| Thumbnail | Separate nonzero UUID and JPEG. |
| Gallery | Zero to six ordered images, each with a separate nonzero UUID. |
| Biography | Optional 1..4096-byte plain UTF-8 text, never HTML. |
| Duration | 1000..360000 milliseconds of network observation. |

Every JPEG must be at most 128 KiB, at most 4096 pixels on either dimension and
at most 4,194,304 pixels total. The maximum configured image bytes are 1 MiB.
Full decoding precedes any device activity. Each role has its own ID even when
bytes happen to be identical. IDs cannot collide with each other or the catalog
GUID. Preserve original source files separately in the application.

The library borrows configuration, strings and JPEG buffers. They must remain
allocated and immutable until `serve` returns. Validation does not transfer
ownership or store provider identity. Biography text is escaped into the device's
Atom HTML field; markup in user text remains text.

## Integrate with music sync

One thread owns the device from entry through return. No scan, heartbeat,
finalization, second sync or concurrent libzune call may use it during the session.
The caller supplies a READY callback:

1. The library validates content, performs the proven reference initialization,
   then negotiates the native USB PPP/IP tunnel.
2. Before serving catalog traffic, it invokes READY once on that same thread.
3. READY calls `zune_hd_art_ensure_artist` with the persisted artist identity.
   It then performs the application's serialized album/track links and covers.
4. Return 0 for success, -1 for definite failure, or -2 if transaction boundaries
   are uncertain. Never finalize or sever inside a callback.
5. The library answers requests and records complete HTTP acknowledgments. It
   closes the tunnel when safe and returns a structured report.
6. Only if `result.safe_to_continue` is true may normal sync/finalization proceed.
   Otherwise stop all device activity, sever, and reconnect before future work.

Progress callbacks are synchronous informational notifications. They must not
call device operations, mutate input buffers, or reenter the session.
`zune_abort` requests cooperative cancellation; do not free/sever the handle
until the active call returns. Individual PTP operations retain their own USB
timeouts. The observation budget does not preempt the initialization sequence or
arbitrary work in READY. An integration must bound its own callback work.

The strict artist inventory matches ASCII case and outer whitespace in the same
way as ordinary artist reuse, without folding different accented Unicode names.
Any ambiguous matching artists, a GUID used by another name, or an existing
artist with a different/empty GUID returns `IDENTITY_CONFLICT` without writes.
Fresh creation uses the existing verified root `Artists` association, the artist's
`.art` filename, `0xDAB0=0`, Name, and `0xDA97` in one property list, followed by
zero-byte commit and exact name/GUID readback. It never creates shared folders.

Inspect `ZuneHDArtistResult` even on failure. `write_attempted`, `created` and a
returned handle preserve evidence of a partially completed creation. Do not fall
back to another creation after a write was attempted. There is no automatic
rollback or hidden deletion. Applications should retain recovery evidence.

## Interpret results honestly

`OK` means every configured image and optional biography received a complete HTTP
response acknowledgment. It does not prove display, persisted cache or slideshow
behavior. `PARTIAL` means the session closed safely but some requests never
arrived or were not acknowledged. A fully cached artist can produce no requests;
that must not be represented as newly delivered content.

Results contain individual image IDs, request/ACK counts, separate biography
counts, READY state, closure state and transport uncertainty. `UNSUPPORTED` on
Classic models performs no USB writes. On uncertain transport, libzune marks the
handle so `zune_sever` closes the USB backend without sending CloseSession into
possibly misaligned framing. Callers still must stop all other device operations.

A biography previously cached empty might not be requested again. The confirmed
fresh-import experiment followed the user's manual on-device deletion and restart.
That is historical test evidence, not a recommended automatic refresh algorithm.

## Buildable client and CLI

`make examples` builds [examples/hd_artwork.c](../examples/hd_artwork.c). This is a
public-header-only client, also exposed as `zunetool hd-art-publish`.

```sh
# Offline validation: does not open a device.
examples/bin/hd_artwork --validate 'Artist' CATALOG_UUID background.jpg thumbnail.jpg bio.txt
# Optional ordered gallery pairs follow the biography path; use - for no bio.
zunetool hd-art-publish --validate 'Artist' CATALOG_UUID background.jpg thumbnail.jpg - GALLERY_UUID gallery.jpg
```

Explicit `--serve` opens the HD and may create a fresh artist. The minimal client
only ensures the artist binding; a music-sync integration must also create/link
its albums/tracks in READY to make that artist relevant to device playback. An
already bound artist may serve from cache and request nothing. The client never
finalizes, deletes or retries. Exit 0 means all content ACKed, 3 means partial,
1 means failure and 2 means command syntax error. It derives a stable thumbnail
ID by XOR 2 on the catalog UUID's last nibble; applications may persist a different
unique thumbnail UUID. The background derivation is protocol behavior.

The older `hd-art-serve` command retains its explicit experiment/journal workflow.
Both clients share the same native implementation under `src/hd/`; the old tool
header paths forward to it rather than maintaining separate packet parsers.

## Software gates and hardening handoff

- `tests/run-hd-artwork-api.sh`: ASan/UBSan public configuration validation,
  UTF-8/UUID/image rejection, Classic/cancel/nested-session guards, exact artist
  reuse/conflict/collision, failed inventory, fresh creation, four-parameter
  create replies, ambiguous commits, stale readback, strict album inventory,
  composed initialization/PPP/no-fetch partial closure, READY cancellation and
  uncertain callbacks, role-level partial completion and biography completion.
- `tests/run-hd-image-server.sh`: 2533 packet/catalog/biography checks.
- `tests/run-hd-network-probe.sh`: 188 PPP/stream/lifecycle checks.
- `tests/run-hd-art-gallery.sh`: 90 role/loading/lifecycle checks.
- `tests/run-hd-http-init.sh`: 193 initialization/refusal checks.

The tests never open physical USB. The composed public no-fetch/cancellation
paths are covered; full public API delivery and ordinary Zuuned music ordering
still need a hardware run. Research delivery remains separately documented in
[HD artwork research](HD_ARTWORK_RESEARCH.md).

For the planned hardening review: audit parser bounds/fuzzing, deadlines across
callbacks and initialization, cancellation memory ordering, strict read allocation
limits, disconnect propagation, multi-artist sessions, identity persistence and
crash recovery before widening distribution. In particular `0x9217` initialization
uses the existing mandatory ZMDB read contract, whose allocation occurs before
our dataset checks. This API does not claim that legacy transport is globally
memory bounded. Static helper modules under `src/hd/` remain shared private
implementation; only `zune.h` is the supported public include surface.
