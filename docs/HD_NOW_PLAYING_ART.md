# Zune HD Now Playing artist backgrounds

Private workstream activated by the user on 2026-10-17. Social-service discussion
is tabled. Both repositories use `feature/hd-now-playing-art`, branched from the
verified spoken-media checkpoints (`d6a0d7c` app, `92d3fc8` libzune).
Existing release/default branches remain unchanged by this work.

## October 19 library and application integration

The user authorized promotion of the working experiment into reusable libzune
code and Zuuned, ahead of a separate code-hardening review. The private branch
now exposes `zune_hd_art_validate`, `zune_hd_art_ensure_artist` and
`zune_hd_art_serve` in the public C header. Shared transport/server sources live
under `libzune/src/hd/`; research tools include the same implementation.
See [the public API guide](../libzune/docs/HD_ARTWORK_API.md) and the public-header-only
example `libzune/examples/hd_artwork.c`. `zunetool hd-art-publish` uses that client.

In Zuuned, right-click a **local artist → Customize → Zune HD** to select a
background, optional separate thumbnail, up to six gallery images and optional
biography. This edits a draft; the existing Apply transaction publishes the
HD manifest with the other customization choices. No USB action happens during
editing or saving. Sources and JPEG derivatives are copied into an immutable
revision under the profile's `hd-artist-art/` directory; original music/image
files and the desktop artist portrait are preserved. Background/thumbnail use
a centered crop; gallery images retain their proportions. Advanced focal-point
editing and provider discovery are not part of this initial integration.

Saved HD content is opt-in per artist and accompanies **new music** queued for
that artist. Start Sync pins the manifest revision, IDs and content hashes.
The worker verifies bounded JPEG bytes before opening the responder. There is
no separate art-only queue action in this version. Clearing the local HD draft
does not remove anything from the device or invalidate its cache.

During the album-organizing phase, `DeviceWorkerHdArtwork.cpp` groups configured
artists and runs one bounded catalog session per artist on the existing device
worker. A READY callback ensures the exact catalog identity, then uses the
ordinary album pipeline with explicit artist handles and strict link/cover
failure checks. `zune_get_albums_checked` supplies complete identity/reference
inventory for this path. Ordinary unconfigured sync retains its existing batch
path. Classic models skip the HD transport and continue ordinary music syncing.

Device serial, connection generation and a unique sync token correlate requests
and results. Cancellation is never cleared by the new workflow. Only a known
usable connection with no attempted artist write may fall back to ordinary
artist creation. An uncertain session or unverified artist creation stops the
batch and releases the connection without another write. No automatic artist
deletion, catalog-ID replacement or cache-bypass identities are used.

The existing queue progress shows artist delivery, and the sync summary/last-sync
record preserves full/partial results and warnings separately from music counts.
Each asset's requests/acknowledgments and biography acknowledgments remain in
the result record, including aborted runs. "Received" means complete transport
acknowledgment, not proof of screen rendering or cache replacement.

**Hardware boundary:** the original research CLI created an artist before its
tracks while the catalog transport was active. The app uploads tracks first,
then creates/links artists and albums inside the catalog READY callback. The
new API and app ordering require a fresh hardware test. Earlier visible image
and biography acceptance does not certify this ordering. Existing unbound or
conflicting artists are refused for HD content without changing their identity;
already-bound artists may reuse cached data and return partial delivery.

For the subsequent hardening review, pay particular attention to cooperative
cancellation during the READY callback, bounded initialization/teardown,
complete device inventory, and the boundary between metadata writes and
acknowledged artwork. Failed album organization preserves uploaded audio but
does not yet provide an automatic repair queue for the retained partial graph.
Re-adding already-synced tracks is deduplicated and is not an artwork-retry or
graph-repair mechanism. Do not work around that by deleting/recreating artists.

Verification lives in `libzune/tests/run-hd-artwork-api.sh`, the preserved
transport/server suites, `tests/hd-artist-art/`, `tests/customize/` and
`tests/hd-artist-art-worker/`. No hardware writes or public pushes are performed
by these tests. This remains a private integration candidate, not a new release.

October 19 integration verification: the native app build passed and the fresh
executable opened on workspace 1 with the existing profile. Public libzune API
tests passed 82 ASan/UBSan checks; shared suites passed 2533 image-server, 188
network, 90 gallery and 193 initialization checks. The public CLI built and
validated content offline. Application checks passed 40 image-store, 64 QML
Customize, 61 actual Apply/persistence and 25 device-worker policy assertions.
Existing music identity (21) and podcast/audiobook worker regression (83) checks
also passed. API reference and TOC freshness checks passed. No test transferred
or deleted physical-device media. No commit, push or release was made.

## Intended result

Provide full-screen artist imagery during music playback on a Zune HD. This is
separate from an album's representative-sample cover and the desktop wallpaper.
Preserve existing album covers, artist identities, music files, spoken media and
custom desktop artwork.

**October 19 display clarification:** treat the Now Playing background and the
artist picture gallery as separate features. The user observes one moving main
background and expects additional pictures to be browsed. A firsthand
[2009 device review](https://unclebubby.blogspot.com/2009/10/zune-hd-review.html)
describes that same separation. Automatic gallery rotation during playback is
not an acceptance requirement. Earlier rotation questions below are historical
investigation, not established firmware behavior. The next requested addition
is artist biography text; earlier sessions advertised that route with empty
content.

**Biography implementation and test:** the private CLI now accepts a validated
plain UTF-8 biography file and serves safely escaped Atom HTML over USB. It
tracks complete biography delivery separately from artwork. The existing
Disturbed refresh requested all six gallery images but did not request the bio;
therefore text delivery and display remain unverified. The user then explicitly
authorized removal and fresh import of the test album/artist. All twenty owned
objects were removed with the 233-object baseline preserved; all sixteen tracks
were restored with the biography available from the start. The HD again fetched
gallery pictures but made zero biography requests during the 240-second test.
The final independent proof matched audio bytes, cover, ordered references and
the exact inventory, followed by one successful reindex handoff. The earlier
empty page may remain cached, but its invalidation mechanism is not established.
The user then deleted the test album on the HD and restarted it. Read-only
inventory confirmed all twenty test objects absent with 233 baseline objects
remaining. Fresh import with the same catalog GUID delivered the biography:
two requests and two complete response ACKs, alongside all eight image assets
(six gallery images, background and thumbnail). The immediate final album
verification failed for an undetermined reason; a separate complete read-only
proof and subsequent proof/handoff both passed, preserving the baseline and
verifying all sixteen tracks, ordered references and cover. One reindex handoff
was confirmed. The user subsequently confirmed the biography worked on the HD.
That passes the visible biography gate for this test; persistence after another
restart and safe replacement on an existing artist remain separate gates. This
sequence is evidence of a successful refresh, not an established cache-reset
contract or permission to delete artists automatically. The production app
does not yet expose biography editing/discovery.

For an accessible protocol walkthrough, read
[How Zune HD artwork travels over USB](../libzune/docs/HD_ARTWORK_TRANSPORT.md).
It explains the in-process server, network packets inside USB, image roles and
the distinction between received bytes and visible artwork.

The initial hardware gate is one existing artist and one selected local JPEG.
Automatic provider discovery and bulk updates follow only after the device
actually displays the image and retains it after disconnect/reconnect. Accepted
transfers and advertised descriptors alone do not establish this.

## Evidence and unknowns

**October 19 latest hardware handoff:** the complete sixteen-track Disturbed
album is restored. Independent live proof matched all 159,318,487 audio bytes,
ordered references, album cover and the exact preserved inventory; one explicit
reindex handoff succeeded. The fresh import fetched four gallery images and the
background. A four-connection limit in the experimental responder prevented
two other image connections. With sixteen bounded slots, an image-only retry
received requests and complete ACKs for the two missing gallery images and the
thumbnail, with peak concurrency five and no capacity refusals. Thus all six
gallery images plus separate background and thumbnail have delivery evidence
across two sessions. The retry closed cleanly; its generic nonzero exit reflects
no new background request, not failed delivery of those three requested assets.
Physical display, retention and rotation remain the user's next test. These
changes are experimental CLI work, not a production app artwork feature.

**October 19 cache-refresh retest:** the explicit-role responder now supports
six gallery images, a separate background and a separate thumbnail. A bounded
180-second session after the user's power cycle still did not request the
Disturbed catalog. It requested older image IDs instead; no new gallery asset
was acknowledged. This does not establish rotation or successful replacement.
The user then removed the test music through Zuuned. A fresh read-only inventory
confirmed all 16 Disturbed tracks and its abstract artist/album were absent,
but the two test-created music folders remained. Fresh-import preflight refused
those names before any upload. Restoration must use a fresh inventory baseline
and verified ownership of any remaining test folders. The user also removed the
earlier Billie probe, so its former baseline is no longer applicable.

- Our Windows captures document PPP-over-USB framing on `0x922C`/`0x922D`
  alongside control operations `0x922B`/`0x9230`. The role of `0x922F` remains
  unresolved; no PPP payload was observed in its datasets. See the corrected
  [capture and hardware evidence](../libzune/docs/HD_ARTWORK_RESEARCH.md).
- The [earlier research](../libzune/docs/SPOKEN_MEDIA_RESEARCH.md) identifies
  external catalog HTTP route leads. These do not prove the HD payload contract,
  correct artist mapping, image constraints or firmware cache behavior here.
- Current artist creation/reuse uses Name and object handles. A local MusicBrainz
  identity, a device handle and a catalog GUID are different identifiers. Never
  substitute one for another without evidence.
- The October 18 authenticated read-only HD probe completed: 83 operations,
  eleven artist properties and six artist objects. The later complete sample
  found both candidate GUID fields DA97/DA99 zero on all six artists. Persistent
  UID DC41 was nonzero but is not a proven catalog identity. The standard
  DeviceInfo fields parsed successfully, with twelve extra bytes explicitly
  reported as uninterpreted. Diagnostic tests pass 144 checks.

## Phases and acceptance gates

1. **Read-only investigation.** Inspect HD-advertised operations and artist
   descriptors; sample existing identity values without changing them. Audit old
   captures for actual catalog requests. Record failures and unknown mappings.
   No setters, object creation/deletion, tunnel initialization or finalization.
2. **Native transport proof.** Implement the smallest capture-supported C
   transport increment with bounded parsing, cancellation and offline malformed
   packet/replay tests. Prove the exchange under a single device owner before
   adding image serving. The final feature must not require host network
   reconfiguration, a web server, Python or another runtime process.
3. **One-image physical proof.** Resolve one exact device/catalog identity;
   serve a prepared local image through the proven protocol. Verify visible Now
   Playing presentation, persistence and reconnect behavior. Document required
   device actions rather than guessing cache invalidation or changing unrelated
   artist IDs.
4. **Application integration.** Use native image preparation and serialized
   DeviceService/DeviceWorker operations. Reuse staged Customize and queue
   feedback. Browsing/selecting candidates does not commit device writes.
   Preserve source files and explicit custom artwork.
5. **Regression and release review.** Verify normal music sync, artist reuse,
   spoken media, disconnect/cancellation and unsupported-model behavior. Package
   after local acceptance; public distribution needs explicit release approval.

## Architecture boundaries

libzune owns transport, bounded decoding and request/response state. Native app
services own identity resolution, JPEG preparation and cached artwork selection.
QML presents state and user intent only.

All MTP transactions, including future tunnel polling, use the existing
serialized device owner. Provider work cannot re-enter USB, block the UI or
outlive its connection generation. Stale image completions cannot write to a
newly connected device. Research tools cannot compete with the running app.

Start with cached local artwork to exclude provider failures from the initial
experiment. USB-only caching is a testable goal, not a supported-feature claim;
the initial investigation must not depend on the user's wireless network.

## Current status

**October 18 full-album follow-up:** a retained Disturbed test transferred all
16 tracks of *Immortalized (Deluxe Edition)*, linked them in order and verified
the separate album cover. The HD acknowledged **three distinct artist JPEGs**
through the USB responder, including both gallery IDs and the background.
Repeated requests produced nine completed JPEG responses, not nine images.
The observation later ended on a packet-processing failure; shutdown completed.
A fresh independent session then verified all 159,318,487 audio bytes, track
identities, ordered references, the album cover and original inventory, followed
by one confirmed reindex handoff. Existing media and
the earlier retained Billie fixture were preserved. Visible rotation and cache
persistence still need physical playback testing. See the dated full-album
evidence in the library research; there is no production app artwork action yet.

The user subsequently identified the visible image as the **band photo**, moving
slowly in the background. This matches the asset served as the dedicated device
background and confirms its animated presentation. No switch to the other two
images was observed. Whether the artist collection participates in another
playback mode remains unknown. Do not equate distinct transport ACKs with a
slideshow or treat movement of one image as image rotation.

The user-provided Zunestar2140 metadata export guide describes a seven-image
workflow plus a separate thumbnail, recommended image dimensions and a boot-time
fetch sequence. Its companion XML/server templates are the next comparison;
the PDF alone does not establish our missing rotation behavior. See the
[guide evidence](../libzune/docs/HD_ARTWORK_RESEARCH.md#zunestar2140-metadata-export-guide-new-evidence)
for page references, differences from our three-image fixture and test limits.

October 19: the downloaded original templates clarify the seven-image layout:
six collection images, one separate device background and a separate thumbnail.
All 150 original templates agree. Unlike our first fixture, background and
thumbnail are excluded from the collection. They advertise collection dimensions
as 1000x1000 while serving smaller prepared JPEGs. These are controlled-test
leads, not a confirmed explanation for missing rotation. The
[template comparison](../libzune/docs/HD_ARTWORK_RESEARCH.md#original-metadata-templates-inspected-october-19)
records roles, provenance and the next experiment; no device change was made.

The subsequent explicit-role implementation passed its software tests and was
tried against the existing Disturbed artist without media or identity changes.
The HD requested only the previous three image IDs, never the updated catalog;
none of the six new gallery members was fetched. The 180-second session closed
cleanly. A full power-off/on fetch test is awaiting the user. The role fix is
implemented in the research tool, but new-gallery hardware delivery and visible
rotation are not yet verified.

**October 18 milestone:** the fully tagged fresh-artist experiment delivered
two JPEG responses over the native USB tunnel, including one acknowledged
`deviceBackgroundImage` response. The HD made three DNS queries and nine HTTP
requests. No external web server or host network configuration was needed.
This establishes image retrieval for the isolated test artist, not visible
Now Playing presentation or a safe retrofit for existing artists. The earlier
negative observations below remain useful comparisons. See the final first-sync
results in the library research for cleanup and remaining hardware gates.
The test's six temporary objects were removed through guarded recovery after
firmware retained its album reference. Exact inventory returned to the original
230 handles. Recovery software gates pass 257 checks. Existing media was preserved.

The read-only probe and capture audit are complete for the tested HD. On
October 18 native bidirectional PPP/IP, DNS and HTTP exchanges passed on hardware
with orderly tunnel closure and no host network configuration. One existing
artist's DA97 was temporarily assigned an experimental routing GUID, with an
exclusive durable backup and exact readback. After physical reconnect, the HD's
ZMDB retained that GUID and correctly linked 37 tracks and four albums. The
original DA97 has since been restored and verified. No artist objects were
recreated and no media was added or removed.

The prepared local portrait has not been delivered: the HD requested only an
old Microsoft secure-clock URL during the first mapped serving attempt, then
no HTTP resources during a 60-second post-reconnect attempt. A generic HTML
response is a diagnostic approximation, not secure-clock provisioning. Neither
observation proves what prevents catalog requests. The next investigation is
the firmware's artwork-fetch trigger/network prerequisites, not rebuilding
artist links. The phase-3 image-display gate remains open. No app upload action
or shipping release changed. The
[library research](../libzune/docs/HD_ARTWORK_RESEARCH.md) records the protocol
evidence, bounded transport, tests and remaining gates.

The follow-up isolated Xune `0x9231` initialization request (258 zero SENDDATA
bytes) was accepted by the HD without a stall, but another 60-second observation
produced no artwork request. The backed-up artist change was again restored
and verified. This tests one missing initialization step, not Xune's complete
sequence. The new native helper passes 80 sanitizer checks.

### Native catalog and full initialization, October 18

The research CLI now serves a coherent one-artist catalog entirely inside the
USB connection: overview, image list and empty biography XML, plus background,
primary-image and image-ID JPEG routes. It handles the exact legacy artwork
hostnames without changing host DNS, opening a host listening port or fetching
from an external server. XML parsing, identity/host restrictions and complete
TCP/JPEG responses pass 961 sanitizer checks. libxml2 is a test-only dependency.

The full six-operation initialization prefix also passed on the HD, including
both complete ZMDB reads. A subsequent six-minute observation completed PPP/IP
and TCP negotiation but recorded zero DNS queries, HTTP requests or image
deliveries. The tunnel closed cleanly. The third temporary artist DA97 binding
was restored and its exact original value verified (`0x2001`). Existing media
and artist objects were preserved.

This closes the missing catalog-response implementation work, but does not pass
the physical image gate. The next controlled experiment should distinguish an
artist update before network setup from an update while the network is ready.
The reference performs creation/retrofit after networking starts; that is a
scheduling lead, not proof that an in-place update triggers downloads. Preserve
the artist object and use the existing durable recovery journal. No bulk sync,
app action or public release is enabled by these research results.

### Network-ready update and direct-push follow-up

The next physical test did apply the identifier after bidirectional IPCP, on
the same serialized connection, with confirmed readback. It still produced no
DNS/HTTP/image request. The user stopped this observation in favor of direct
push investigation after approximately 218 seconds including setup/cleanup.
The tool closed the tunnel and restored the original identifier automatically,
with confirmed readback. No image reached the device.

Direct upload remains unproven. The artist object does not advertise the image
property used by album covers, and its `.art` object is size-zero metadata.
Offline inspection of the local HD firmware package found the actual cache
filename `nowPlayingBg.jpg` beside artist XML/image routes. Mapping its owning
code and USB accessibility is now the investigation target. No firmware was
flashed, media deleted or public build changed. See the library research for
exact offsets, provenance and the distinction between a filename and a writable
device endpoint.

### First-sync experiment

The original [ZuneArtistImages setup guide](https://github.com/spidersandmoths/ZuneArtistImages#using-the-server-and-zune-setup)
distinguishes a newly tagged first sync from content already on the device.
Its linked tagging helper maps the MusicBrainz release-artist ID to
`ZuneAlbumArtistMediaID`; the reference USB writer supplies DA97 when creating
the artist metadata object, then links the track through DAB9. This is a
different experiment from changing an existing artist's identifier. The guide
is a lead, not proof that creation alone triggers a request on this firmware.

The user authorized an isolated fresh-artist/short-track test on October 18.
The native responder must already have completed network negotiation before
creation, with all USB operations serialized. Use synthetic audio, a unique
test name and a durable recovery manifest. Existing artist objects and media
must not be deleted or recreated. Record creation, readback, actual HTTP/image
traffic and cleanup independently; accepted metadata is not image delivery.

The initial read-only preflight found existing `Artists` and `Albums` roots,
but no `Music` root. The experiment therefore tracks a newly created temporary
Music folder separately and removes it last, only if its identity is unchanged
and it is empty. Software verification covers both existing-root preservation
and owned-root cleanup.

The first live creation received `0x2001` with four response parameters. The
diagnostic expected exactly three and stopped before creating any artist or
track. Normal libzune already accepts at least three parameters for this
operation. This is a diagnostic contract mismatch, not a negative artwork
result. The recovery manifest and complete pre-write inventory were retained
to identify the single newly created empty Music root without touching existing
objects. Subsequent results are recorded in the library research document.

### Retained visual test

The user requested a retained Billie Eilish or Halestorm example on October 18.
Use a separate, clearly labeled Billie Eilish test artist and a 30-second copy
of local audio, leaving the original Billie artist and albums unchanged. The
test clip and portrait stay in ignored private evidence, never source exports.
The explicit retain mode must verify the complete graph, audio readback and
acknowledged background response before recording its retained state. Keep the
manifest for later identity-checked cleanup; do not automatically remove this
test before the user inspects the unplugged HD. Visual presentation and cache
persistence remain user acceptance gates.

The retained test now exists as `HDArtworkProbe-BillieEilish-66de4016cf30`.
The HD acknowledged its background JPEG and returned `0x2001` for the single
reindex request. All 230 original handles remain, plus exactly six test objects.
Do not clean these up before the user inspects the unplugged HD. The library
research records the verification/reporting discrepancies and the exact retained
handles and manifests; physical display is not yet confirmed.

## Multiple images and selection design, October 18

This is an investigated design, not an implemented app feature. Preserve the
retained Billie fixture while physical presentation is checked.

### What the evidence supports

The firmware contains separate `images.xml` and `nowPlayingBg.jpg` resources.
Its gallery route requests `/images?chunkSize=10`; its background route requests
`/deviceBackgroundImage?width=480&resize=true&contenttype=image/jpeg`.
Ten is a requested page size, not a demonstrated image limit. Neither these
strings nor successful JPEG delivery prove that gallery pictures rotate during
Now Playing or idle display.

The [reference server](https://github.com/spidersandmoths/ZuneArtistImages/blob/main/main.py)
advertises six gallery images, serves a separate background and has a thumbnail.
That count is its implementation choice. Our current experimental server has
one image ID and serves the same JPEG for all image roles. Two acknowledged
JPEG responses therefore do not mean two distinct images were stored.

Microsoft specifies a [480 by 272 display](https://news.microsoft.com/source/2009/08/13/media-alert-zune-hd-available-for-pre-order-today/),
or 272 by 480 in portrait. Display dimensions and image payload dimensions are
different contracts. The reference scales larger images to 480 pixels wide while
preserving aspect ratio; it does not crop them to the physical panel. Our single
480 by 480 JPEG passed transport, with physical rendering still unverified.

### Proposed user flow

1. Right-click a local artist and choose **Customize**, using the existing
   staged sheet. Add an **HD artwork** area alongside the desktop portrait.
   Keep the artist's desktop image and album covers independent.
2. Add local pictures through drag and drop or a multiple-file picker. Reuse
   the horizontal thumbnail strip for selection. Provider candidates can join
   this flow later without changing its persistence contract.
3. Explicitly choose the **Now Playing background**. Keep a separate gallery
   selection; describe it as an artist gallery until hardware demonstrates its
   playback/idle behavior. Allow one source to serve multiple roles. Preserve
   selection order locally without promising firmware playback order.
4. Show a portrait device preview, with an optional landscape preview. Support
   moving the focal point, zooming, and resetting the framing. A Fit option may
   add padding; Fill may crop. Never stretch. Until hardware crop behavior is
   verified, label the preview approximate and retain extra image area.
5. **Apply** saves locally and atomically, following the existing Customize
   completion/error behavior. Cancel discards the draft. Preparing or choosing
   images must not modify the device. An explicit connected-HD queue action
   stages the saved revision for Sync through the existing island.

Do not add a permanently visible Customize button, a separate transfer queue,
or device writes when selecting a thumbnail. Offline selection and editing are
allowed; device queue admission still requires a connected compatible device.

### Native preparation and persistence

Preserve original media files. Copy chosen image sources into an app-owned
immutable artwork store on successful Apply, so a later moved source file does
not break the saved choice. Persist role assignments, ordered gallery asset
IDs, normalized focal/crop coordinates, source hashes and a revision. A crop
change creates a new derivative; resetting framing uses the original.

The existing portrait cache is keyed by artist name and stores one image. Do
not overload it with HD sets. Use a stable local artwork-set UUID and revision,
with explicit ownership migration on artist rename and collision refusal.
Provider IDs, local artwork IDs, device object identities and catalog GUIDs
remain distinct. Export source artwork, never the desktop halftone/worn display
cache. The existing centered `PreserveAspectCrop` preview is not an editable
or persisted crop tool; native framing and preview/export agreement are new work.

Extend the native Customize save pipeline rather than doing image IO in QML.
Workers apply EXIF orientation, normalize color to sRGB, flatten alpha onto the
chosen matte and write JPEG derivatives atomically. Bound input bytes, decoded
pixel counts, output bytes and aggregate memory. Proposed baseline JPEG output
must be validated on hardware; do not infer a firmware byte cap from the
research responder's 128 KiB guard. Record actual encoded dimensions in XML.
Use a versioned conversion profile so hardware findings can change output
without discarding originals or manual selections.

Initially retain the proven square transport fixture and test 480-wide,
aspect-preserving variants. Do not lock production assets to 272 by 480 until
the device's crop/pan behavior is observed. Thumbnail generation is a separate
role; the reference's 160 by 120 thumbnail is a test candidate, not yet our
hardware-validated requirement.

### Library and device boundaries

The app owns source selection, provider resolution, image preparation and
persistence. libzune receives a bounded immutable catalog snapshot containing
artist routing identity and separate background, thumbnail and gallery assets.
Each distinct gallery asset needs a unique image ID and exact route mapping;
unknown IDs must not fall back to another artist or image. Keep artist identity
stable across artwork revisions. Resolve asset-ID/cache revision semantics in
the protocol experiment before exposing replacement as a supported action.

All HTTP responses remain inside the native USB tunnel under serialized device
ownership. No hosted server or host network configuration is needed by the
current transport proof. Prepare snapshots before USB work and pin them for the
session. Bound catalog size, assets, requests and transfer time; handle partial
writes, retransmissions, cancellation and disconnect without publishing partial
success. Track requested routes, complete acknowledged responses and distinct
asset IDs separately. Delivery is not proof of firmware display or persistence.

Existing-artist replacement is a separate release gate. An external
[Xune user report](https://github.com/xune-software/xune-releases/issues/36)
describes persistent old artwork and cross-artist changes; its cause is not
established and is not a finding about our code. It reinforces the need to test
cache replacement and artist isolation explicitly. Never delete/recreate real
artists or change catalog identities to force a refresh. Keep the durable
identity journal and refuse unsupported replacement truthfully.

### Delivery phases and gates

1. **Confirm current display.** Inspect the retained Billie fixture unplugged,
   during playback and idle, then check persistence after reconnect. Preserve
   its existing objects until explicit cleanup is requested.
2. **Prove distinct images.** Extend the research responder with three visibly
   different numbered gallery images and a separate background. Verify catalog
   XML, unique IDs, actual dimensions, exact routing and per-asset ACK tracking
   offline before a separately reviewed isolated hardware experiment. The
   current responder stops after its first background ACK; the multi-image
   experiment must continue for a bounded observation window so it does not
   cut off later gallery downloads. Observe
   which roles are requested, displayed, rotated and retained. Then test six,
   seven and pagination boundaries as warranted. None is a claimed firmware cap.
3. **Prove framing and replacement.** Compare square, portrait and landscape
   sources carrying edge/focal markers. Test a second revision without changing
   the artist's identity, followed by reconnect/reboot and another artist's
   sync. Confirm unrelated art and music remain unchanged. Cache invalidation
   and existing-artist fetch triggers remain unresolved requirements.
4. **Build local editing.** Add staged HD artwork selection, previews and native
   preparation/persistence. Verify cancel, stale Apply, invalid/oversized files,
   moved originals, orientation, crop reset, offline use and desktop-art
   preservation. Use existing design tokens and Customize UI conventions.
5. **Integrate Sync.** Add typed HD-art queue admission and correlated progress,
   with device serial/session guards and truthful pending/received states.
   Regression-test music, artist reuse, spoken media, unsupported Classic
   devices, interrupted transfers and reconnection before packaging.

Start with a small bounded selection supported by experiments, not an unlimited
gallery or an advertised maximum copied from the reference server. Current work
adds this plan only; no multi-image hardware write or production UI is enabled.
