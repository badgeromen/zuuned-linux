# Zune HD Now Playing artist-art research

Active private workstream, 2026-10-17 onward. This concerns full-screen artist
imagery during music playback on a Zune HD, separately from album representative
sample covers. Native PPP/IP and HTTP exchanges now work on the tested HD.
One existing artist's DA97 was temporarily assigned an experimental routing GUID
with durable recovery and exact readback. No artwork request or image delivery
has been observed. Now Playing image presentation remains unproven. See the
October 18 experiment below for the restoration outcome.

## Evidence boundaries

- Our captures prove PPP framing, network negotiation and some IPv4 traffic.
  They do not prove an HTTP artist-catalog request, image response contract or
  image cache behavior.
- The live HD probe proves advertised operations/property types and sampled
  existing values. Advertised write access is not a hardware-tested write path.
- A device artist handle, persistent unique object identifier, catalog GUID and
  MusicBrainz ID are different identifiers. No established mapping permits
  substituting one for another.
- The external HTTP route lead recorded in
  [spoken-media research](SPOKEN_MEDIA_RESEARCH.md#current-evidence) remains a
  reference lead, not evidence of what our tested HD requests.

## Capture evidence

The 2026-10-17 audit inspected ten existing official Windows client USB captures
from the local `Downloads/zune` collection. The audit used tshark `usb.capdata`,
selected split-PTP container reassembly, HDLC unescaping and PPP CRC-16 checking.
It was entirely offline. Frame numbers below are Wireshark frame numbers; for
split DATA transfers a cited DATA frame identifies the container header and
its payload follows separately.

USB descriptors identify `albumsync.pcapng`, `episodepull.pcapng` and
`photosync.pcapng` as Zune HD (`045e:063e`). The other seven captures enumerate
`045e:0710`. The decoded PPP protocols are LCP `0xC021`, IPCP `0x8021`, CCP
`0x80FD` and IPv4 `0x0021`, with valid CRCs on the reconstructed frames.

| Capture | Frames | Observation |
| --- | --- | --- |
| `tvcopytocollection.pcapng` | 547, 837 | Device sends `CLIENT` through `0x922D`. |
| Same | 1296, 1298, 1300, 1303 | Host `0x922C` command, DATA header, `CLIENTSERVER` payload, response reporting 12 payload bytes. |
| Same | 1417, 1436, 1445 | LCP Configure-Request, host Ack/Request, device Ack plus initial IPCP request. |
| Same | 1470, 1479, 1504, 1513 | Further IPCP and CCP negotiation. |
| Same | 1513, 1597, 1685 | IPv4 UDP DHCP Inform from device. |
| Same | 1767, 1786, 1797 | TCP SYN/SYN-ACK/ACK; zero application payload. Four cycles appear in this capture. |
| `albumsync.pcapng` (HD) | 4028, 4049, 4060, 4081 | Four 40-byte DNS proxy messages over TCP, ports 49193/49950: A/IN queries for `stats.zune.net` and NXDOMAIN replies. |
| Same | 739 | Artist creation DATA (`0x9808`, format `0xB218`) contains `0xDAB0` UINT8 zero, `0xDC07` filename STR and `0xDC44` name STR. No catalog GUID in this dataset. |
| `episodepull.pcapng` (HD) | 237 | Requests `0xDC41` property descriptor for artist format `0xB218`; this does not establish catalog identity. |

No HTTP artist-catalog or `deviceBackgroundImage` request was recovered from
the PPP exchanges. Apart from the four HD DNS messages above, decoded TCP
packets had no application payload. Raw `HTTP/1` strings in `newzune` frames
228/238 and `USB-capture` frame 242 are within large binary `SendObject`
`0x100D` uploads, starting at DATA frames 166 and 174 respectively. They are
not observed HTTP network traffic.

The three HD captures contain no individual `GetObjectPropValue` or
`SetObjectPropValue` calls for identity candidates `0xDA99`, `0xDC41`,
`0xDA97` or `0xDAB9`. This scoped finding does not rule out fields inside
opaque ZMDB downloads and does not prove that an identity is absent on a device.

### Corrections to historical PPP claims

The corrected transport description and per-capture transaction counts are in
[vendor operations](ZUNE_VENDOR_OPS.md#0x922c--0x922d-ppp-transport-0x922f-unresolved-status-dataset).

- LCP option `02`, length 6, all-zero value means **ACCM=0**, not MRU=0.
- `0x922F` DATA has **1048 total bytes: 12 PTP + 1036 payload**. The first
  16 payload bytes vary among four uint32 tuples; the remaining **1020 bytes
  are zero in all 208 reconstructed datasets**. No PPP content was observed
  there. A status structure is a hypothesis; "bulk PPP buffer fetch" is unproven.
- The initial temporary audit summary mistakenly totaled these datasets as 245.
  Summing the preserved per-capture counts gives 208; this document supersedes
  that aggregate. The individual counts and zero-tail observation are unchanged.
- `tvcopytocollection` has 20 `0x922C` command transactions, not 40; counting
  command and DATA headers together inflated the old number. `capture-bulkreading`
  has 59 `0x922F` commands, not approximately 130.
- Polling is not uniformly 1 Hz: `tvcopytocollection` commands 486 and 490
  are at 24.117495 and 24.130485 seconds, about 13 ms apart.
- Captured interleaving does not establish the Windows client's thread model
  and does not authorize concurrent transactions on our serialized device owner.

### Capture provenance

The captures contain private device/media/authentication data and are not
committed. These SHA-256 values identify the exact offline evidence without
publishing its contents.

| Capture | SHA-256 |
| --- | --- |
| `USB-capture.pcapng` | `2ce861a098a45e2f7f21f74817626cbdf557a4cd4191361d9a9586a32da21b41` |
| `album copy.pcapng` | `3f7d9a604027427c095923f146d1452c02e95d20561d417d63d7540c4fd1670e` |
| `albumsync.pcapng` | `4c35aa3f6c029a187d6aded61e34de30615041b83f6bb45cc758f06d27f25c53` |
| `capture-bulkreading.pcapng` | `6109e15d844aee8723f1b33ac3f5ba9df031c9d9156895e99aa436f6d52125da` |
| `episodepull.pcapng` | `0034704fd54539c4002522e2f55c45d91e938ff2198e1e8c51d06478e6876f54` |
| `episodesync.pcapng` | `0bb062ea68944ffa3bdfe02c5b9f0a5aefa259c06799e32e5bef569de4b069ca` |
| `newzune.pcapng` | `d13ff5c2334499a7c6a5ccfc26528abf9363fbe5967983d0ea0c80749f60315b` |
| `photocopy.pcapng` | `8ed0b37b77af2799c71aab47556a0834bbaef9d495759225494dd59791efb78d` |
| `photosync.pcapng` | `50754ccc1c910ae2c669e5fefc552e817fa04b6e1ee617216c46e845d213625f` |
| `tvcopytocollection.pcapng` | `975c53f70c3b1898f9fefd458d19734f79139a420df6dd05466141f43dd922ca` |

## Live read-only HD probe

The successful phase-1 report, reviewed 2026-10-18, is retained privately in the
parent application at `build/hd-art-evidence/hd-art-probe.txt`, SHA-256
`c4e9254889fd6244c6805a38c84c79ea5bb8cfe7f3373cd95ac404ba51000709`.
It reports `status complete`, 83 operations, 11 artist properties, six artists
and three sampled artists. This document records counts/types and zero/nonzero
classifications only, omitting names, handles, serials and actual identity bytes.

| Artist property | Advertised type/access | Observed sampled values |
| --- | --- | --- |
| `0xDA99` | UINT128, read/write | All zero for all three sampled artists |
| `0xDA97` | UINT128, read/write | All zero for all three sampled artists |
| `0xDC41` | UINT128, read-only | Nonzero for all three sampled artists |
| `0xDC44` | STR, read/write | Well-formed existing names |
| `0xDC07` | STR, read/write | Well-formed existing filenames |

Other advertised properties were `0xDC04`, `0xDAB0`, `0xDC4F`, `0xDC02`,
`0xDC0B` and `0xDC01`. `0xDAB9` was not in this artist property list. These
are descriptor headers, not certification of every default/form or a write
contract. Nonzero `0xDC41` does not make it a catalog artist identifier. Zero
`0xDA99`/`0xDA97` does not authorize generating or assigning replacement GUIDs.

`zunetool hd-art-probe [1..8]` limits identity sampling and permits only
GetDeviceInfo, GetObjectPropsSupported, GetObjectPropDesc, GetObjectHandles and
GetObjectPropValue after connection/authentication. No image serving, setters,
object creation/deletion or PPP initialization is part of the probe. Its
accepted dataset/count limits do not bound the underlying PTP receive allocation.

The software fixture is `tests/run-hd-artist-probe.sh`; it uses a strict
read-transaction allowlist and malformed-input/cancellation tests without USB.
The independent October 17 sandbox rerun encountered LeakSanitizer's ptrace
restriction. The unrestricted run passed all 144 checks under ASan/UBSan and
LeakSanitizer. Keep hardware probe completion separate from software test
execution and from artwork acceptance.

## Native tunnel handshake gate, 2026-10-18

`zunetool hd-tunnel-probe --ack-lcp` passed on the connected Pavo HD after
confirming the application had released USB. The sanitized report is preserved
at [hd-lcp-gate.txt](../tests/fixtures/hd-art/hd-lcp-gate.txt). This independently
validates the initial exchange, not full PPP negotiation or artwork delivery:

1. Send the 260-byte open control from HD `albumsync` frames 97/99/101, using
   `0x922B(3,1,0)`, then `0x9230(1)`. The opaque control prefix is
   `02 1d c3 00`; remaining bytes are zero. Its internal meaning is unknown.
2. Poll `0x922D(3,3)`, receive `CLIENT`, answer `CLIENTSERVER` via `0x922C`.
3. Receive a CRC-valid LCP Configure-Request, identifier 0, with ACCM=0,
   Protocol-Field-Compression and Address/Control-Field-Compression options.
4. Send Configure-Ack preserving the exact request identifier and options.
5. Confirm `0x9230(2)` and `0x922B(3,2,0)` with zero-length DATA. Release the
   ordinary PTP session. Tool exits zero and reports complete.

The tool guards HD family and successful authentication. It does not write
artist IDs, objects, images or host network settings. Default invocation observes
the request without sending Configure-Ack; `--ack-lcp` explicitly enables that
single acknowledgement. It permits only the observed option set, validates
HDLC escaping/FCS/lengths, and supports split or coalesced identical requests.
Unexpected protocol content fails explicitly rather than being guessed.

There are at most 80 polls with 250 ms pacing and a 20-second scheduling deadline.
The deadline cannot interrupt an existing synchronous USB transaction; existing
PTP timeouts still apply, including cleanup. A lost response suppresses further
tunnel commands, with cleanup reported unknown. Ordinary `zune_sever` still
attempts PTP CloseSession during handle release. SIGINT/SIGTERM request orderly
cleanup while framing remains valid. There is no blind tunnel retry.

Idle `0x922D` can return RESPONSE without DATA. The new internal opt-in
`PTP_DP_GETDATA_OPTIONAL` preserves that response and its parameters. It validates
transaction/opcode/lengths and caps DATA payload allocation at 64 KiB, separately
from the existing fixed receive scratch buffer. Ordinary mandatory GETDATA does
not accept idle success. Rejected headers cannot change outbound framing mode.

Software gates, all passed without USB under ASan/UBSan/LeakSanitizer:

- `tests/run-hd-tunnel-probe.sh`: 211 checks, including captured frame fixtures,
  malformed input, cancellations, deadline and cleanup failures.
- `tests/run-ptp-optional-data.sh`: 24 scripted USB cases using real PTP code,
  including idle responses, split data, ZLP, stale/truncated/oversized headers
  and ordinary no-data/send-data/mandatory-read behavior.
- `tests/run-hd-artist-probe.sh`: 144 checks.
- Existing podcast and audiobook protocol regressions: 100 and 74 checks.

This initial handshake milestone was followed by the full network experiment
below. A successful acknowledgement does not establish request triggering,
image format, persistent cache behavior or Now Playing presentation.

## Native network and one-artist experiment, 2026-10-18

`hd-network-probe` completed bidirectional LCP and IPCP on Pavo, with host
192.168.55.100 and device 192.168.55.101, no payload compression and no host
network changes. The HD sent DHCP Inform packets and initiated TCP to port
49949. The image-serving variant negotiates the virtual host as DNS server.
All USB operations remain serialized. These private CLI experiments do not
change the public library API or application behavior.

`hd-art-serve <routing-guid> <jpeg>` uses bounded native IPv4, UDP/TCP DNS and
HTTP handling. It has four TCP flows, one outstanding segment per flow,
checksums, sequence validation, bounded retransmissions and 4 KiB request
buffers. DNS answers are limited to configured catalog and observed fwlink
hosts. The JPEG is loaded before USB access, limited to 128 KiB, and served
only for the configured GUID's `deviceBackgroundImage` route. Other routes
return 404 except the explicitly diagnostic fwlink approximation. No host
socket, interface, web server or runtime helper is required by this transport.
The 60-second scheduling window and send budget are bounded; synchronous USB
timeouts can extend actual elapsed time. TCP acknowledgement is not a physical
display acceptance test.

All six existing artist objects were sampled before writes: DA97 and DA99 were
zero on all six. `hd-art-bind` then backed up one exact existing artist into an
exclusive mode-0600 journal, fsynced both file and directory, and assigned only
DA97. The GUID is an experimental local routing key, not a historical catalog
or MusicBrainz identity. Readback confirmed it while preserving object handle,
name, filename, storage, DC41 and DA99. No artist was recreated and no music
was uploaded or deleted. `hd-art-restore` checks the same device and complete
artist snapshot before restoring the old DA97; unknown outcomes never authorize
a blind retry. The native-ABI journal is private local recovery data.

The test used an existing cached portrait, prepared as a 480-square JPEG of
23,236 bytes without changing the source. The live server saw DNS for
`go.microsoft.com` and two `GET /fwlink/?LinkId=25817 HTTP/1.1` requests. It saw
no artist catalog request and acknowledged zero images. After the user's
physical unplug/replug, DA97 still matched the experimental value, but another
60-second window showed only PPP/IP and a TCP handshake, with no DNS or HTTP.
Both observation windows closed the network mode and stream cleanly.

The post-reconnect read-only ZMDB v5 dump confirmed the exact experimental GUID
in that artist's optional field `0x14`, with 37 music records (fixed offset +4)
and four album records (fixed offset +0) referring to the same artist atom.
Dump SHA-256: `9ceea68461ac6f760925058009b96cfc16039880c220f949ec8434ad1ce0d150`.
Thus the identifier reached the actual media database and existing references
were intact. Recreating or relinking the artist is not supported by this evidence.
The original DA97 was restored afterward: `artist-binding-restore confirmed`,
PTP `0x2001`, complete guarded readback. The private recovery journal is retained.

The reference labels fwlink as a connectivity probe and returns generic HTML.
Microsoft instead identifies this exact LinkId as a
[secure-clock URL](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-drmcd/57809984-3dc2-4c1b-911e-46d3fa4600c9).
Its protocol requires correlated, signed responses. Our HTML response is only
a diagnostic approximation and does not provision that clock. The reference
example allows a five-minute wait, so our 60-second observations cannot prove
that the HD will never request artwork. Nor do they prove secure-clock failure
is the reason artwork was not requested. No clock/authentication changes were
attempted.

Additional software gates pass under ASan/UBSan with leak checks:

- `bash tests/run-hd-network-probe.sh`: 92 checks.
- `bash tests/run-hd-image-server.sh`: 355 checks.
- `bash tests/run-hd-artist-binding.sh`: 289 checks.
- `bash tests/run-hd-art-journal.sh`: durable roundtrip, restrictive permissions,
  overwrite/symlink/FIFO refusal, corruption and truncation checks.

The four captured 40-byte TCP messages now have an independent interpretation:
an eight-byte big-endian header `[source-id, 53, total-length, 0]` precedes DNS;
responses swap the first two words. Frames 4028/4060 query `stats.zune.net`,
4049/4081 reply NXDOMAIN (`0x8583`). This corroborates the pinned reference's
special DNS framing, but does not establish an image request.

## External implementation leads

### Isolated Xune initialization step tested

After reviewing the author's guide and pinned Xune sequence, the optional
`hd-art-serve <guid> <jpeg> --init-9231` experiment sends precisely one `0x9231`
request: no parameters, SENDDATA, 258 zero bytes. It guards authenticated Pavo
and cancellation, records actual responses, and stops on refusal, transport
failure or unexpected response parameters without retrying. It does not run
Xune's entire initialization sequence or add it to normal sync.

Hardware returned `0x2001 OK`, no response parameters and no observed stall.
The existing PPP/IP/TCP exchange then succeeded, but the following 60-second
window contained no DNS/HTTP request and zero acknowledged images. Tunnel
mode-end and closure both completed. The temporary artist DA97 assignment had
its own durable backup and was restored with confirmed exact readback afterward.
This demonstrates acceptance of the correctly formed command on this HD, not
that it triggers image downloads. The historical blanket stall warning cannot
be applied to this exact request. No undo semantics for `0x9231` are known or
claimed. Private reports: `hd-art-init9231.txt` and
`billie-init9231-restored.txt` in the parent app's ignored evidence directory.

`bash tests/run-hd-http-init.sh` passes 80 ASan/UBSan checks with leak detection,
covering exact payload, guards, refusal, transport loss, malformed reply counts,
cancellation and report failure. `zunetool` rebuild passes.

Xune's remaining prefix is `9217(1), 9218(0,0,5000), 9217(1),
9219(0,0,5000), 922F()` before tunnel setup. `9217` returns the full database,
not just a response; any further experiment must consume and validate those
DATA phases and preserve single-owner serialization. The one-step test does
not establish whether the complete sequence will trigger artwork.

The author's [April 25 reverse-engineering guide](https://www.badgerbytes.xyz/zunes-are-still-awesome-breaking-into-the-zune-the-complete-guide-to-reverse-engineering-microsofts-dead-mp3-player/)
was reviewed during the October 18 investigation. It records PPP transport,
artist references and the optional ZMDB GUID field, and credits magicisinthehole,
XuneSyncLibrary and NiceBeard for foundational research. Its historical warning
lists `0x9231` among stalled operations, while the pinned Xune source supplies
258 zero bytes with SENDDATA. That exact-payload experiment is distinct from
probing the opcode without its required data. The article does not document a
direct HD artist-background upload. Later capture evidence in this document
supersedes its `0x922F` bulk-buffer interpretation and old byte counts; the current
split-mode ZLP rule uses payload length and abstract objects commit zero bytes.

Inspected a local read-only clone of XuneSyncLibrary at commit
`c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4` on 2026-10-18. Its repository
`LICENSE` contains LGPL version 2.1. No implementation code was copied into
libzune for this research milestone; attribution and license provenance remain
separate from independently captured behavior.

Its `magicisinthehole/android-file-transfer-linux` submodule was inspected at
`eb36f23ee213475c77bcecaa0898732f251e80b1`. The special DNS framing and ZMDB
artist-field leads were checked against our own captures and live database,
respectively. The native PPP implementation also follows
[RFC 1661](https://www.rfc-editor.org/rfc/rfc1661) and
[RFC 1332](https://www.rfc-editor.org/rfc/rfc1332). Reference comments alone do
not certify unknown vendor operations or safe existing-artist updates.

- [`CreateArtistMetadata`](https://github.com/xune-software/XuneSyncLibrary/blob/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/ZuneMtpWriter.cpp#L240)
  optionally writes a supplied 16-byte GUID as `0xDA97` UINT128 while creating
  an artist, alongside Name, ObjectFileName and `0xDAB0`. That is a creation
  recipe in another implementation. It does not establish safe updates to our
  existing artist objects, correct catalog GUID assignment, or image-display
  acceptance on our device.
- [`HTTPParser.h`](https://github.com/xune-software/XuneSyncLibrary/blob/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/protocols/http/HTTPParser.h#L12)
  documents the expected route
  `/v3.0/en-US/music/artist/{uuid}/deviceBackgroundImage?width=480&resize=true&contenttype=image/jpeg`.
  This is a concrete route lead. Our captures do not show it being requested,
  and the comment is not a tested image dimension/encoding contract here.
- [`MetadataRequestHandler.cpp`](https://github.com/xune-software/XuneSyncLibrary/blob/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/protocols/http/MetadataRequestHandler.cpp#L259)
  adds Content-Disposition with an inline filename, Last-Modified,
  Cache-Control `no-cache` and ETag when serving a local file for that endpoint.
  These reference response headers are not yet a capture-validated requirement
  for our implementation.

## Next gates

### Catalog service gap identified, 2026-10-18

The user's retired-server hypothesis led to two additional source references:

- [XuneArtistImagesServer](https://github.com/xune-software/XuneArtistImagesServer),
  inspected at `393c13c813ef32953376a3efc383a8e2b5eee586`. Its `main.py`
  exposes artist overview, biography and image-list Atom XML, plus background
  and image-ID JPEG routes. Image-list entries contain `art.zune.net` URLs.
  Its README credits the upstream spidersandmoths project. No implementation
  code was copied or service launched.
- [ZuneNetApi](https://github.com/ZuneDev/ZuneNetApi), inspected at
  `ff8e36374b049d000b9b9227fe5e69e1c4f70cf4`. The artist controller likewise
  implements overview, image list, biography and background JPEG endpoints.
  This is independent support for the service surface, not evidence that every
  route must precede a background request on our HD.

The [original image-server setup](https://github.com/spidersandmoths/ZuneArtistImages)
redirects `catalog.zune.net`, `image.catalog.zune.net` and `mix.zune.net` through
the connected PC. It describes initial sync of tagged music as the metadata
fetch trigger and a full device power cycle as troubleshooting. This supports
fixed legacy service names; we have not extracted firmware strings to prove
their precise storage location. XuneSyncLibrary also explicitly maps
`art.zune.net`, `social.zune.net` and `go.microsoft.com` in its DNS interceptor.

Before the catalog increment, the prototype answered only `catalog.zune.net` and `go.microsoft.com`,
and served only the configured `deviceBackgroundImage` route plus diagnostic
fwlink HTML. Other catalog routes returned 404 and other A/IN host queries received
NXDOMAIN. Consequently that version was not a complete replacement catalog service.
However, prior tests logged no catalog/image DNS or HTTP request, so these
missing responses cannot yet be called the observed cause of failure.

### Native catalog implementation and full-prefix hardware test

The research CLI now provides the coherent one-artist XML/JPEG set from a local
JPEG and explicit artist name. It does not open host sockets, change DNS/hosts,
fetch remote content or run an external service. It answers only the configured
artist and derived local image ID; the image ID is never written to device
metadata. The supported surface is:

- `catalog.zune.net`, `image.catalog.zune.net`, `art.zune.net`, `mix.zune.net`
  and the separate diagnostic `go.microsoft.com` hostname.
- Artist overview, `/images` and `/biography` Atom XML. Biography content stays
  empty rather than inventing artist information.
- `/deviceBackgroundImage`, `/primaryImage`, `/v3.0/en-US/image/{image-id}`
  and `/1/{image-id}/504/image.jpg` JPEG routes.
- Bounded UTF-8 artist names and XML escaping, coherent image links/dimensions,
  exact GUID/host validation and validated origin/absolute HTTP targets.

Four TCP flows, 4096-byte requests/XML buffers, a 128 KiB source JPEG and a
512-packet send budget bound the experiment. The default observation is still
60 seconds; `--seconds` accepts 1 through 360. The deadline bounds scheduling,
not time spent inside a synchronous USB transaction. `background_completed`
requires the background-route bytes to be acknowledged; other JPEG routes have
a separate counter. Neither counter proves display or persistent caching.
The secure-clock fwlink response remains diagnostic HTML, not valid signed
clock provisioning.

```sh
./zunetool hd-art-serve <routing-guid> <local-jpeg> \
  --artist-name 'Exact artist name' --init-full --seconds 360
```

Input validation precedes USB connection. The mutually exclusive `--init-9231`
option preserves the previous single-operation experiment. Full initialization
adds `9217(1), 9218(0,0,5000), 9217(1), 9219(0,0,5000), 922F()` after `9231`.
Received database buffers are checked and freed without logging their content.
Mandatory `9217` GETDATA still uses the existing transport allocation without
a preallocation cap; optional receives use the 64 KiB limit. Do not describe
the whole initialization path as bounded-memory because the catalog is bounded.

On the physical HD, all six operations returned `0x2001`: the two database
payloads were each 237896 bytes, `9218` returned 252 bytes and three zero response
parameters, `9219` returned four bytes and three zero parameters, and `922F`
returned 1036 bytes without response parameters. Network negotiation then
completed. Over six minutes the device sent three DHCP Inform packets and a
TCP SYN/ACK exchange to port 49949. There were **zero DNS queries, zero HTTP
requests and zero acknowledged images**. The three rejected packets in the
server report were the unsupported DHCP packets, not failed image requests.
Mode-end and stream closure completed normally.

The temporary Billie Eilish DA97 binding used a new durable journal and was
restored afterward: exact original-value readback confirmed, response `0x2001`.
No artist objects or media were created/deleted. Private evidence in the parent
app's ignored `build/hd-art-evidence/` directory:

- `hd-native-catalog-full.txt`: sanitized initialization/network report.
- `billie-catalog-binding.txt`, `billie-catalog-restored.txt`: guarded write and
  restoration results; `billie-catalog-recovery.bin` is retained privately.
- Corresponding `*.private.log` files contain connection diagnostics and must
  not be copied into public documentation or source exports.

Software verification: `tests/run-hd-image-server.sh` passes 961 ASan/UBSan
checks, including actual libxml2 parsing, response lengths, full JPEG bodies and
negative host/path/GUID cases. libxml2 is required only by that test runner.
The expanded full-prefix fixture passes 193 checks, native network scheduling
and failure handling 106, and CLI release handling 12. All use ASan/UBSan with
leak detection and no USB. The serving CLI now carries transport uncertainty
through initialization, polling and tunnel cleanup. Healthy completion/refusal
keeps orderly session close; uncertain transport releases the USB handle and
owned resources without sending CloseSession. This is a CLI-only policy and
does not alter the application's normal teardown. It does not roll back device
writes or downloaded artwork; the guarded journal restoration remains separate.
This is still a research tool, not a new public library API or app feature.

### Binding after network readiness

The opt-in `--bind-on-ready <handle> <new-backup-file>` CLI experiment changes
only the ordering of the existing-artist DA97 assignment. It requires an explicit
`--artist-name` and the reserved single-artist routing GUID. The default serving
command still does not write artist metadata.

Before initialization, it reads the exact artist/device identity and original
zero DA97, then saves an exclusive durable mode-0600 recovery journal. The
serialized network tick applies once after both IPCP directions are ready and
the received PPP frame is complete. It rereads the guarded artist identity and
checks the signal flag and monotonic deadline immediately before the setter.
Renegotiation does not repeat the write. No artist or media object is recreated.

After a clean tunnel shutdown, the same owner restores and verifies the original
DA97 even when observation ended by a healthy timeout or SIGINT. Actual device
abort/disconnect flags are not cleared. An uncertain write or transport prevents
further commands; retain the journal for separate guarded recovery. Restoration
only concerns DA97, not any image cache the firmware may have populated.

```sh
./zunetool hd-art-serve <reserved-routing-guid> <local-jpeg> \
  --artist-name 'Exact artist name' --init-full --seconds 360 \
  --bind-on-ready <exact-artist-handle> <new-private-recovery-file>
```

Software gates: 60 ready-binding orchestration checks, 316 artist binding checks
(including cancellation/deadline at the actual setter boundary), 112 network
checks, 193 initialization checks and 12 CLI release checks pass with sanitizers.
Durable journal tests and five new invalid CLI argument cases pass. Independent
read-only QA found no blocker to the isolated hardware test. These checks do
not establish firmware fetching, display or caching.

Physical result: the HD completed initialization and bidirectional IPCP, then
accepted the in-place assignment with exact guarded readback (`0x2001`). It
sent three DHCP Inform packets and a TCP handshake but no DNS/HTTP request.
The user redirected the investigation to direct push before the six-minute
deadline, so this run was deliberately interrupted with SIGINT. Approximately
218 seconds elapsed from journal creation through the final report; this is
not a completed six-minute observation. Mode-end/stream-close succeeded and
the same connection restored the original DA97 with confirmed readback
(`0x2001`). Private evidence: `hd-bind-after-ready.txt` and
`billie-ready-recovery.bin`. No JPEG delivery was observed.

### Direct-push investigation

The user requested a host-initiated path rather than more idle request polling.
No direct artist-background write contract has yet been established:

- The live artist descriptor list omits `RepresentativeSampleData` (`0xDC86`).
  `zune_brand()` writes album/track representative samples; it is not evidence
  that an artist background can use that route.
- `src/album.c` and the pinned Xune writer create `.art` objects with size zero
  and an empty commit. The filename suffix does not establish a JPEG container.
- Xune's cache callback caches HTTP responses on the host, not firmware files.
- The advertised operation list lacks service operations `0x9301` through
  `0x9309` and partial-object send `0x95C2`. Uncharacterized vendor operations
  are not inferred to be image setters. This does not prove no direct path exists.

The historical [ZuneConsole guide attached to this issue](https://github.com/ZuneDev/PyZuneCatalogServer/issues/1)
also describes a local HTTP replacement and first-sync-driven retrieval, not
a direct file upload contract. Its erase/resync workaround was not performed.

Offline inspection of the user's existing `PavoBaseline.cab` now gives a
firmware cache lead. The extracted `EXT.bin` contains UTF-16 strings
`nowPlayingBg.jpg`, `images.xml`, `bio.xml`, `music/artist`, directory templates
and `/deviceBackgroundImage?width=480&resize=true&contenttype=image/jpeg`.
The background filename occurs at file offsets `0xD1275F`, `0x1050393` and
`0x1094AE3`. Nearby directory configuration names include `Artist`, `AlbumArt`,
`DataPartition` and `Software\\Microsoft\\Zune\\Directories`. These strings
do not yet establish an absolute cache path, writable MTP handle, upload
operation or that this exact firmware build is installed on the connected HD.

Provenance: local cabinet SHA-256
`f674a79af0db514aa934e4a630ee385f0445f9fab33bfe632da328fe55fd1702`;
extracted EXT.bin SHA-256
`c2255daab91fbf013a0dd615da6ebb52865de511910e57a1700514b123821b63`.
Cabextract reported 7384 possible trailing bytes in the cabinet; source bytes
were preserved. Firmware and derived dumps remain in ignored private evidence,
not source exports. Nothing was flashed or installed on the device.

The offline CE container/ROM-TOC mapping validates all 126 record checksums and
bounds and maps 109 modules. The three background filename occurrences belong
to `gemstone.exe`, `zmedia_serv.dll` and `znet_serv.dll`, respectively.
`MtpSvc.dll` is a separate module. In `znet_serv.dll`, a candidate cache-path/URL
routine at runtime address `0x41AC12A0` (container offset `0x10ECB67`) references
the background filename through an ARM literal at `0x41AC1300`; its literal
pool also pairs the other artist XML filenames and endpoint strings. A matching
routine occurs in `zmedia_serv.dll` at `0x41A4C51C`. This identifies code to trace,
not an externally callable image setter or a verified cache-write routine.
Routine starts are candidates based on the preceding ARM PUSH and confirmed
PC-relative literal references, not a full control-flow disassembly. The ignored
firmware evidence directory preserves `inspect-rom.py`, `rom-module-map.json`,
`rom-cache-paths.txt` and `rom-cache-findings.json` for reproduction.

## Fresh-artist first-sync comparison

The original [ZuneArtistImages setup instructions](https://github.com/spidersandmoths/ZuneArtistImages#using-the-server-and-zune-setup)
call out first sync of properly tagged music and distinguish it from content
already on the player. The linked Zune Modding Helper maps MusicBrainz's
release-artist ID to `ZuneAlbumArtistMediaID`. The pinned reference writer
supplies DA97 as UINT128 during artist creation and DAB9 as a UINT32 artist
handle on the track and album. Its normal upload also builds music folders and
album references. Editing DA97 on an existing object does not test that entire
creation sequence.

On October 18 the user authorized one fresh isolated artist/album/track test
with the native network responder already ready. Synthetic low-volume audio
avoids modifying or reusing the user's media. The experiment must record each
mutation and returned handle durably, preserve existing objects and stop on
uncertain transport/write outcomes. Cleanup may remove only identities proven
created by that invocation. A reduced graph or a failed preflight is not a
negative result for the complete first-sync hypothesis.

The HD had `Artists` and `Albums` roots but no `Music` root. The experiment now
tracks a missing Music root as its own temporary object, after a second complete
absence check once the network is ready. Existing roots are preserved. Creation
and cleanup gates cover the complete five-object graph plus this optional root.

The first physical root creation returned success with four response parameters.
The diagnostic incorrectly required three and stopped before creating any artist,
album or track. Production `mtp.c` already accepts at least three parameters;
the diagnostic now accepts three through five and journals the complete raw
response before interpreting it. This failure is not an artwork-trigger result.

The retained manifest contained exactly 230 original handles and a terminal
root-creation intent. A separate read-only recovery probe found exactly one new
object: the empty root-level Music association. Its serial/storage, persistent
identity, parent, filename, zero size and empty children matched. The guarded
recovery command persisted proof, repeated the checks, deleted only that object,
and verified exact restoration of the 230-handle baseline. No existing media was
deleted. Tests now pass 203 sanitizer checks, including response-count handling,
manifest corruption, uncertain writes, cancellation during recovery validation,
foreign references/children and exact baseline restoration.

After a USB transport reset, the next run created the test graph while PPP/IP
was ready and observed a DNS query for `mix.zune.net`. It stopped at track
readback before setting album references: the HD normalized Track 1's filename
to `01 <title>.mp3`. Name, parent, artist handle, size and all 48,970 MP3 bytes
matched. The diagnostic now recognizes this exact Track 1 naming behavior;
other identity checks remain strict. A DNS query during this run is not proof
that an artwork request was triggered or that an image was delivered.

On reconnect the unlinked empty test album was absent, and firmware had created
an Unknown Album/Unknown Artist pair referring only to the test track. Complete
inventory and inbound reference checks proved all 230 original objects remained
and isolated the seven additions. Guarded recovery removed only the five
surviving returned test IDs. A subsequent reconnect removed the firmware pair;
complete inventory again matched the original 230 IDs exactly. No existing
artist or media object was deleted. These recovery and filename gates pass 229
sanitizer checks. The next fixture has matching embedded title, artist,
album-artist, album and track tags, using an explicitly bounded unique probe
name. Network-handshake failures before creation remain separate from an
artwork-trigger result.

Bounded startup diagnostics subsequently captured a repeated six-byte `CLIENT`
(`43 4c 49 45 4e 54`) after the host had already replied `CLIENTSERVER`, before
any PPP frame began. The original diagnostic incorrectly fed that greeting to
the PPP decoder and stopped. This is direct evidence of a startup retry, not
malformed IP or an artwork-provider error. Startup greeting handling must allow
bounded retries and stream fragmentation/coalescing, while retaining strict
framing once PPP begins. Diagnostic byte output is limited to 64 bytes from
the authenticated tunnel and does not capture MTPZ authentication traffic.

## October 18: first acknowledged background image

The startup parser now accepts at most four complete CLIENT greetings before
the first PPP flag, including fragmented and coalesced greetings. Once PPP
starts, the original strict frame checks apply. All 179 ASan/UBSan checks pass,
including transport fixtures for the actual repeated greeting and a greeting
coalesced with the first PPP frame.

With that build, the fully tagged synthetic MP3 experiment created and verified
the Music root, two media folders, fresh artist, album and track after IPCP.
The artist's DA97 was present at creation; album and track DAB9 referred to
that exact new artist. The album linked the test track, whose 49,167 bytes
matched the fixture. The device then requested:

- `catalog.zune.net` artist overview, albums, tracks, biography and similar artists;
- `image.catalog.zune.net` image-ID JPEG, artist images list and
  `deviceBackgroundImage?width=480&resize=true&contenttype=image/jpeg`;
- the artist model route, with `mix.zune.net` also observed in DNS.

The native server recorded **3 DNS queries, 9 HTTP requests, 2 completely
acknowledged JPEG responses and 1 acknowledged background response**. Three
unsupported catalog requests were rejected; no connection timed out. The tunnel
closed cleanly. This is the first confirmed local-image transport result, using
only serialized USB and the native responder. It does not prove screen rendering,
cached persistence, or which individual difference from the earlier attempts
caused the firmware to request images.

Automatic cleanup stopped before deletion: clearing the test album references
returned success, but readback still contained the test track. A subsequent
read-only inspection found exactly the six intended additions, every original
handle still present, correct persistent identities and matching audio bytes.
The retained manifest records this cleanup boundary; do not interpret the
command's nonzero exit as failure to deliver the acknowledged images.

Guarded recovery subsequently removed only the six journal-proven additions,
deleting the test track first. Full before/after inventory checks confirmed all
230 original handles remained and the exact baseline was restored. No existing
artist or media object was deleted. The completed-graph recovery and automatic
firmware-pruning cases pass 257 ASan/UBSan checks and independent code review.
This recovery mode accepts only the exact recorded creation/link/failed-clear
history, not arbitrary manifests or user media. The experimental normal cleanup
still attempts reference clearing and may need this recovery on this firmware;
production cleanup integration remains future work.

Private evidence (ignored, not exported):
`build/hd-art-evidence/first-sync-tagged-retry.txt`, its durable `.manifest`,
`first-sync-tagged-inspection.txt`, `first-sync-tagged-recovery-inspect.txt` and
`first-sync-tagged-recovery-delete.txt` in the app checkout. The JPEG fixture is
480×480, 23,236 bytes, SHA-256
`97fd6771a54388a8d0ad34e821bc49674681c53d12a292e45123d9019ec3e4c8`.
The last recovery log explicitly reports `baseline inventory restored`.

## Remaining image gates

### Retained visual-test tooling

The October 18 follow-up adds explicit `hd-art-serve --first-sync ... --retain`.
It retains only after a successful clean network exchange, acknowledged background
response, complete artist/album/track graph, exact identity/reference/inventory
checks and byte-for-byte audio readback. A durable retained record precedes one
`CleanDataStore` (`0x9108`) handoff. A refused or uncertain handoff preserves the
already-retained test and reports failure, with no retry or automatic deletion.
A failed retention proof never reports success merely because rollback worked.

Short fixtures may be at most 1 MiB; `--first-sync-duration-ms` accepts 1000 through
60000, defaults to 3000 and verifies the supplied duration on readback. Names may
use the old random probe form or `HDArtworkProbe-<ASCII letters>-<12 hex digits>`.
Existing-name absence remains mandatory. The user-visible test uses a separately
tagged Billie Eilish clip; it does not rename, recreate or reuse the existing
Billie artist or albums. Recovery accepts the exact retained journal history and
protects an existing shared Music root. These gates pass 391 ASan/UBSan checks
with mocked PTP, plus independent review. Hardware results follow separately.

Do not remove retained visual-test objects before the user has inspected them.
Keep the exact source fixture and manifest for later `hd-art-recover-graph`
inspection and explicitly requested removal.

### Retained Billie Eilish hardware example

The retained visual test is `HDArtworkProbe-BillieEilish-66de4016cf30`, with a
30-second locally prepared excerpt of BIRDS OF A FEATHER and the same 480×480
portrait. Original Billie artist `134219606` and the other existing artists were
left unchanged. The test created artist `134220976`, album `100666554`, track
`16780473`, folders `83889328`/`83889329` and owned Music root `83889327`.

This run recorded ten HTTP requests, one DNS query, two acknowledged JPEGs and
one acknowledged background response. The final immediate retention proof
stopped without a diagnostic identifying the failed stage. Subsequent read-only
inspection proved all 230 original handles, exactly six intended additions,
matching persistent IDs/DA97/DAB9, the sole correct album reference and all
481,341 audio bytes. The transient failure's cause is not established; added
stage diagnostics do not justify claiming a firmware bug.

Explicit recovery `--retain` repeated that complete proof and durably recorded
preservation, then sent exactly one CleanDataStore. The HD returned transaction
result zero, response `0x2001`, and one response parameter. The diagnostic had
expected zero parameters and reported failure despite the successful device
response. The extra parameter's meaning/value was not captured in stdout and
is not inferred. The diagnostic now accepts zero or one parameter only for
CleanDataStore and prints future raw values without interpreting them. More than
one remains an error. The complete first-sync/retention/recovery suite passes
425 ASan/UBSan checks; the CLI rebuild passes. No handoff retry or deletion was performed. The objects remain
for the user's unplugged Now Playing inspection; screen presentation is pending.

Private evidence is `billie-retained.txt`, `billie-retained-inspect.txt`,
`billie-retained-handoff.txt` and their manifests in the app's ignored
`build/hd-art-evidence/` directory. Preserve `billie-retained.manifest` and
`billie-retained-clip.mp3` as the original cleanup authority. Do not run deletion
until the user requests removal after visual testing.

### Multiple-image offline investigation, October 18

Offline firmware inspection distinguishes an artist collection from a dedicated
background cache entry. `images.xml` pairs with `/images?chunkSize=10`, while
`nowPlayingBg.jpg` pairs with
`/deviceBackgroundImage?width=480&resize=true&contenttype=image/jpeg`.
These pairs have literal references from shared catalog/cache routing routines
in `gemstone.exe`, `zmedia_serv.dll` and `znet_serv.dll`. No slideshow caller,
rotation timer, retained-image count or cache eviction policy was established.
Ten is a request page size, not a demonstrated maximum.

Provenance: the existing extracted Pavo `EXT.bin` SHA-256 is
`c2255daab91fbf013a0dd615da6ebb52865de511910e57a1700514b123821b63`.
Reinspection validated 126 B000FF records and 109 module TOC entries. In
`gemstone.exe`, file offsets are `0xd1271b` (`images.xml`), `0xd12733`
(collection route), `0xd1275f` (`nowPlayingBg.jpg`) and `0xd12783`
(background route). References map through uncompressed o32 sections; routine
boundaries remain candidates rather than recovered symbols. Ignored private
evidence includes `build/hd-art-evidence/multi-image-firmware-findings.md` and
`pavo-firmware/multi-image-xrefs.json` in the parent application checkout.

The [ZuneArtistImages implementation](https://github.com/spidersandmoths/ZuneArtistImages/blob/main/main.py)
chooses six gallery images, a separate background and thumbnail. Its count is
not a firmware limit. The [ZuneNetApi implementation](https://github.com/ZuneDev/ZuneNetApi)
at `ff8e36374b049d000b9b9227fe5e69e1c4f70cf4` emits distinct collection image
IDs/dimensions and preserves aspect ratio when resizing to a requested width.
These are implementation leads, not Microsoft specifications. Our responder
currently shares one JPEG buffer across image routes; two full JPEG ACKs do not
prove two distinct stored pictures.

Next: a bounded immutable catalog with distinct asset IDs, separate background,
thumbnail and gallery roles, exact route lookup and actual JPEG dimensions.
Measure requests, complete acknowledged responses and unique assets separately.
Test three distinct numbered images before exploring counts/page boundaries;
verify physical playback, idle presentation, persistence and revision replacement.
The current serve loop exits after background completion and first-sync graph
completion. Extend that lifecycle to a bounded observation window for gallery
experiments; otherwise a missing later download may be a host-side early exit.
The tool's 128 KiB JPEG guard is not a known device limit. The app should preserve
originals and prepare versioned derivatives; firmware asks for width 480 without
a height, so fixed panel cropping is not established. Cache replacement and
artist isolation need their own hardware gates, including a second revision and
another artist's sync. This investigation used no USB and left the retained
Billie fixture unchanged.

### Full-album and multiple-image experiment, October 18

The user requested a complete local album with multiple artist images, without
removing existing content. The prepared fixture is 16 ordered MP3 tracks from
Disturbed's *Immortalized (Deluxe Edition)*, 159,318,487 audio bytes and roughly
66 minutes. Source files remain unchanged. The separate album cover comes from
the local source's embedded image. Three distinct width-480 artist JPEGs have
independent IDs and hashes. Media and image provenance stay in ignored evidence
under `build/hd-art-evidence/disturbed-album/` in the parent app checkout.

The multi-image responder now supports an immutable bounded asset table, exact
ID routing, actual dimensions, separate background/primary choices and per-ID
and distinct-content ACK counts. Six total assets is an experimental memory
budget, not a firmware limit. Multi-image observation no longer exits on the
first background ACK. Full-album transfers have a separate durable journal and
an additional bounded final-proof budget after clean network teardown. Neither
mode enables a production app UI or changes existing artist identities.

Read-only preflight passed with 236 existing objects, including the retained
Billie example. The first full-album attempt created and verified its own media
folders, artist and album, then stopped after the first track's successful
SendObject response because its stricter metadata verification differed from
firmware readback. No automatic deletion or SendObject retry occurred.

Read-only evidence established these distinctions:

- The first track's title, artist, duration (80,591 ms), track number, artist
  linkage and normalized filename were correct. The album-name property read
  `Unknown Album` before album references had been installed.
- Pavo returned zero for `0xDAB8` after sending default disc one. The retained,
  linked Billie track independently showed the same default-disc normalization,
  while its album-name property correctly reflected the linked album. This does
  not establish normalization for disc two or higher.
- After the failed session, firmware pruned the newly created empty abstract
  album and introduced an `Unknown Album`/`Unknown Artist` pair. The placeholder
  album references exactly the first transferred track. Its artist link points
  to the placeholder artist; the track itself still points to our intended
  artist. Both placeholders have parent zero and distinct persistent identities;
  the placeholder album's filename is the counted empty string.
- All 236 baseline handles remained. Inventory contained exactly the surviving
  test folders, intended artist, first track and two firmware placeholders.

The original journal remains authoritative. Recovery must first inspect exact
live identities and audio bytes, distinguish provisional from linked album
checks, and treat unknown write outcomes as unresolved. Replacing a proven
missing album created by this experiment is distinct from deleting/recreating a
user's existing album or artist. Placeholder recognition needs exact graph and
persistent-identity checks, never names alone. Physical multi-image delivery and
display are still pending at this point in the experiment.

The subsequent continuation uploaded the remaining fifteen tracks and verified
all sixteen track identities after installing ordered album references. The
album cover also passed exact readback. The image responder then recorded
**three distinct image IDs and three distinct JPEG contents acknowledged**,
including four background-route completions. Nine JPEG responses completed in
total; repeated responses are not additional images. The device requested both
gallery IDs, including the separately selected primary image.

This run ended on an invalid/unsupported network packet after those deliveries.
Tunnel shutdown completed, but the conservative completion gate refused the
final album-retention/handoff claim. The journal and all media were preserved;
no automatic deletion or re-upload followed. Final independent album proof and
physical image presentation remain separate from these transport results.
Evidence: `album-resume-clean.txt` and its continuation manifest in the ignored
Disturbed evidence directory. Private stderr contains authentication diagnostics
and must not be published.

Offline analysis found that the reported packet was a valid PPP/IP/TCP ACK.
Replaying that exact frame reproduced the failure at the responder's shared
512-send budget. Artwork sessions now use a bounded 8,192-send budget under the
same wall-clock deadline; plain probes retain 512. Budget exhaustion has a
specific diagnostic instead of being reported as an invalid packet. The old
live log did not record the send counter, so budget exhaustion is the leading
code-path explanation, not a directly logged hardware counter. Network tests
pass 188 sanitizer checks, with 46 gallery and 1,630 responder checks passing.
This correction has not yet been rerun in a hardware artwork session.

**Independent final proof passed.** A fresh authenticated session reconstructed
the exact original and completed continuation journals, verified their hashes
and the current sources, then read back all sixteen tracks byte for byte:
159,318,487 bytes matched. Track/root/artist/album identities, ordered references,
the exact album cover, the original 236-handle baseline and the bounded recorded
placeholder graph all passed. No media was re-uploaded or deleted. A new durable
proof journal preceded one successful `0x9108` reindex handoff. The helper exited
zero; evidence is `album-final-proof.txt` and `album-final-proof.manifest`.
The album is retained for physical playback and artist-image rotation testing.
This final check did not reopen or retest the image tunnel.

**User screen observation:** after disconnecting to test the retained album,
the user reported seeing only one image, then identified it as the band photo
moving slowly in the background. That is the dedicated background asset in this
test. Its animated presentation is confirmed; switching to either gallery image
was not observed. Other playback modes remain untested. Three distinct
acknowledged JPEG payloads must not be
described as three persistently cached or visibly rotating images. Firmware's
separate `nowPlayingBg.jpg` and `images.xml` roles are a concrete lead; the
collection's UI consumer and persistence still need to be established. No
additional transfer or device change was made in response to this observation.

### Zunestar2140 metadata export guide: new evidence

The user supplied *Flavor Your Music: How to export your artist's background
images and biography to your Zune HD*, credited to Zunestar2140. The local file
is `Downloads/Zune Metadata Exportation Guide.pdf`, 36 pages, with PDF creation
metadata dated May 19, 2021. SHA-256:
`88db624b54ee1a2e5716a1563428399f8b0db1ded2d133d1a31205dc547cb7ac`.
Page numbers below refer to PDF pages, counting the cover as page 1.

- Page 24 describes seven artist background images (`1.jpg` through `7.jpg`)
  plus a separate `thumb.jpg`. This is evidence from the author's workflow,
  not an independently measured firmware maximum. Recommended artist geometry
  is width 480 and height 328 through 1008; thumbnail geometry is 160 by 120.
  The author reports sluggish visual playback with larger images.
- Our test assets are 480x480, 480x397 and 480x320. The last is below the guide's
  recommended height. This is a test variable, not proof of a firmware rejection:
  all three payloads were acknowledged, and the 480x397 gallery image also did
  not visibly replace the background. Our primary image was not a separate
  160x120 thumbnail.
- Page 18 identifies companion `htdocs` XML documents and `abyss.conf` as the
  image-delivery templates. The PDF does not expose their complete gallery
  schema or URL mappings. Compare those files against the native responder
  before guessing that numbered filenames or image counts alone enable rotation.
- Pages 29–31 describe introducing a tagged artist-header track, powering down
  the device, then booting while attached to the responder. The author reports
  an artist scan beginning about 12–15 seconds after the home screen, with time
  varying by library size, and advises leaving the device UI alone during it.
  Page 34 recommends waiting about 20 seconds after metadata requests start.
  These are reported observations, not hard timing guarantees or proof that
  the same restart is required by our native implementation.

This guide strengthens the case for investigating multi-image presentation.
It does not yet identify why our device displays only the dedicated background.
Next compare the original templates, prepare role-appropriate dimensions, and
test a complete retrieval session with the corrected packet budget before
claiming persistent gallery storage or rotation. The guide's Windows server
and hosts-file setup is not required by our demonstrated USB responder. Its
deletion instructions are not authorization to remove existing user content.
Only documentation was changed while reviewing this guide; no USB writes ran.

### Original metadata templates inspected, October 19

The user supplied both original and customized artist-background restore ZIPs.
The original outer archive contains `Zune Metadata Exportation by
Zunestar2140.zip`; its SHA-256 is
`a06a779867aab033dba9e2ecb111de0c456a47e7dc363abb18b5aa6735c94b3d`.
Only selected XML/configuration/text files were extracted for inspection into
the ignored `build/hd-art-evidence/metadata-guide/` directory. No bundled
executables were run, server installed, host configuration changed or USB used.

All 150 original templates have the same image-feed structure and routing after
normalizing their artist/image GUIDs. All 150 customized image feeds match that
structure too. The guide's seven images have these distinct roles:

| Role | Original template mapping | Our three-image test |
| --- | --- | --- |
| Artist collection | Six entries in `images.xml`, independent IDs routed to `1.jpg` through `6.jpg` | All three assets advertised, including the background and primary |
| Device background | `deviceBackgroundImage` routes to `7.jpg`, excluded from the image feed | Band photo, also in the image feed |
| Overview thumbnail | Overview image ID ending in the template's `10` slot routes to `thumb.jpg`, excluded from the image feed | Singer close-up, also in the image feed |
| Declared collection dimensions | Every instance advertises 1000x1000 | Actual prepared JPEG dimensions |

The customized archive confirms the XML dimensions need not equal the delivered
JPEG dimensions in this workflow: sampled images are width 480 with heights
382–720, and sampled thumbnails are 160x120. The XML still declares 1000x1000.
The template performs static URL rewrites and discards query strings, rather
than resizing dynamically. It returns 404 for albums/tracks/similar-artist
requests. There is no slideshow timer or special rotate command in these
templates. The XML's collection IDs, instance IDs and URLs agree, as ours do.

This narrows the next experiment to role separation, declared catalog dimensions
versus prepared image dimensions, and completion/cache behavior. It does not
prove that six gallery entries are mandatory, that 1000x1000 is a firmware
requirement or that role overlap caused the failure. The earlier firmware
width-420 selection lead alone cannot explain a difference between advertised
480 and 1000, since both exceed 420.

The accompanying bounded firmware follow-up strengthens two points. In this
Pavo image, `gemstone.exe` routine at runtime `0x2190c` obtains collection/cache
kind 6, iterates `feed/entry`, and stops at six attempted entries (`cmp r5, #6`
at `0x21aa0`). Each entry goes through the candidate selector at `0x216b0`,
which checks maximum instance width against 420 and a nonzero entry ID. Neither
bounded routine reads height. Thus six has evidence for this particular gallery
loader, not a universal cache maximum or a requirement to supply exactly six.
Both our declared width 480 and the template's 1000 satisfy this width gate.
These routines establish collection selection, not the downstream display or
rotation caller; image height/decode checks elsewhere remain possible.

Before the next hardware test, support explicit collection membership separate
from background/thumbnail assets, with bounded capacity sufficient for the
template's six-plus-one-plus-one arrangement. Preserve existing single-image
behavior and distinguish acknowledged collection assets from all configured
assets. Catalog source dimensions and served derivative dimensions should be
modeled separately if supported, rather than globally hardcoding false JPEG
dimensions. Test changes independently where possible, using the corrected
packet budget and a complete fetch session. Existing artist/media deletion is
not part of this comparison; no safe cache-refresh contract has been proven.
Audit summaries and selected original files are in the ignored evidence
directory (`template-audit.json`, `routing-audit.json`, `original-images.xml`,
`original-overview.xml`, `original-abyss.conf`). Credit Zunestar2140 for the
original guide/template research; these files are reference evidence, not
vendored application implementation.

### Separate-role implementation and existing-artist test, October 19

The native responder now has an explicit role mode: six ordered gallery assets,
a separate device background and a separate thumbnail, with an eight-asset
bounded table. The image feed excludes both non-gallery roles. Existing legacy
single/multiple-image behavior remains available. `--thumbnail UUID JPEG`
selects explicit mode and cannot be combined with `--primary`. All JPEGs are
fully decoded before connection. The 160-wide thumbnail accepts the observed
480-wide resize request without claiming to upscale its bytes; other roles keep
exact prepared-width checks. Actual JPEG dimensions remain in the catalog.
Gallery and overall asset ACK counts are separate. Completion waits for the
background and gallery plus a quiet interval, never a required thumbnail fetch;
an active transfer prevents early quiet completion. The hard deadline remains.

Software gates passed: 2,073 responder, 66 native gallery, 188 network, 30
preconnection gallery CLI and five album CLI checks. Independent review found
no blocking issue. The built tool SHA-256 was
`c15c3969691a7aa0c72afa7260db8e4a2f7f9cbd175ecc117219f0294cb7b7c2`.

The first physical attempt preserved the existing Disturbed artist GUID and
album. A preceding read-only proof matched all sixteen audio payloads, references,
cover and recorded inventory. Six distinct supplied Disturbed pictures and a
160x120 thumbnail were prepared from the user's customized reference archive,
with new image IDs; the known working band background remained unchanged.
Some supplied pictures are shorter than the guide recommends. This run tests
role separation with actual supplied files, not a new minimum-height policy.

The 180-second image-only session did **not** fetch the new catalog/feed.
All 80 HTTP requests used the three old image IDs. The unchanged background
bytes completed 23 times through their ordinary image-ID route, with no
`deviceBackgroundImage` request and zero ACKs for the six new gallery IDs or
thumbnail. Therefore the separate-role layout was not exercised on hardware.
The session ended at its deadline and closed mode/stream cleanly. No artist
binding, media creation, deletion or reindex handoff was sent by this attempt.
Evidence is `disturbed-roles/serve.txt` and `before.txt` in the ignored app build
directory. Private stderr must remain private.

After the user's power cycle, a second 180-second session again did not request
the Disturbed catalog. Nine requests included old Disturbed and Billie probe
routes; no new image was acknowledged. The responder closed cleanly without
exhausting its outbound budget (54 of 8192). Exact routing rejected unknown IDs
rather than returning Disturbed pictures for another artist. Evidence:
`disturbed-roles/serve-reboot.txt`.

The user subsequently chose to remove the experimental artist through Zuuned.
GUI logs confirm deletion of all sixteen Disturbed tracks and the earlier Billie
probe track, followed by a successful CleanDataStore handoff. The fresh wildcard
inventory has 235 handles; Disturbed's abstract artist, abstract album and tracks
are absent, while owned folders 83889330 and 83889331 remain. Fresh-album
preflight refused those folder names before uploading. The original 236-handle
baseline is no longer applicable after these intentional user deletions. Any
cleanup of these two leftovers must verify journal ownership, persistent IDs,
parents and complete live child inventory. This user-directed experiment does
not authorize automatic artist recreation in the production app.

The two-folder cleanup passed live read-only inspection, then removed only the
journal-owned album folder and artist folder, in that order. Each deletion
revalidated serial/storage, persistent IDs, parent/name/format/zero size and
children using both complete inventory parent reads and filtered enumeration.
The inventory changed exactly 235 to 234 to 233; shared Music and every other
handle were preserved. Eight mock safety cases and four malformed-input cases
passed before execution. Evidence: `disturbed-roles/folders-read-only.txt`,
`folders-cleanup.txt` and the private exclusive cleanup journal. Fresh-album
preflight then passed using the new baseline.

The following fresh import retained the same routing GUID and triggered new
overview, background and image-feed requests. Sixteen tracks uploaded and the
album links/cover completed. Four distinct gallery assets and the background
were acknowledged, but gallery members 1 and 5 and the thumbnail were never
requested. Packet evidence identifies TCP admission pressure: the four-slot
responder kept an idle proxy connection alongside active transfers, while two
gallery connections repeatedly sent SYN without progressing to ACK/GET. The
request logger runs per decoded packet, so this is not merely lost log lines.
The responder now has sixteen fixed slots plus capacity-refusal and peak-flow
diagnostics. A full table never evicts an active transfer. Independent review
and 2420 server, 66 gallery, 188 network and 35 CLI checks passed. The new binary
was built only after the live run ended.

That run's final album proof refused completion. Subsequent read-only inventory
showed exactly 253 objects: the 233-object baseline plus the twenty intended
album graph objects, with no missing baseline or unexpected objects. This
snapshot alone does not establish the reason for the earlier proof failure;
complete payload/reference/cover verification and explicit handoff remain
required before handing the restored album back for testing.

Both the subsequent read-only proof and the separate explicit handoff session
passed: all sixteen payloads matched the current local sources byte for byte
(159,318,487 bytes), ordered references and cover matched, foreign collection
links were absent, and the exact 233-object baseline plus twenty created objects
was preserved. One journaled `0x9108` handoff succeeded. The original final-proof
failure's exact cause remains unestablished; later passing proofs do not erase
that result. Evidence: `normal-proof-read-only.txt`, `normal-final-handoff.txt`
and the private proof journal. No track was retransmitted to obtain this proof.

The subsequent 120-second image-only retry with sixteen slots requested exactly
the two missing gallery IDs (`0297d475-...` and `e39f1966-...`) plus the separate
thumbnail (`6dc1cf5d-...`). All three responses were fully acknowledged. Peak
concurrency was five, with zero capacity refusals and zero timed-out flows.
The session closed mode/stream cleanly at its deadline. The generic command
returned nonzero because it requires a background-route ACK in every session;
the HD did not re-request the already-delivered background this time. This is
not a claim that all assets were fetched in one run. Across the fresh-import
and retry sessions, all six gallery IDs, background and thumbnail have complete
ACK evidence. Persistence, gallery presentation and Now Playing rotation still
require the user's physical observation. Evidence: `flow-retry.txt`; tool SHA256
`e90cf7e1a6045baa19fe28beffa08ef94f56bd9dea3bb7545b3c60cdbe0e7c83`.

### October 19 biography response and existing-artist refresh

The user identified biography text as the next missing part. Previously the
responder advertised `/biography` but returned empty Atom content. The private
CLI now accepts `--biography utf8.txt`: 1..4096 bytes of plain UTF-8, fully loaded
and validated before USB access. Invalid UTF-8, embedded NUL/XML controls,
oversize and whitespace-only files are refused. Text is escaped into generated
HTML, then escaped into the reference Atom `content type="html"` element.
Configuration remains atomic and immutable after traffic starts.

Biography requests and complete response ACKs are counted independently of
images. An explicit biography refresh requires its own complete ACK and does
not require cached backgrounds to be requested again. Fresh-artist/full-album
creation still requires its separate background proof. Software checks pass:
2533 server, 82 gallery/lifecycle, 188 network and 43 CLI refusal checks, with
independent review. The built tool SHA256 is
`79ace818631e918d1fe69a0f4e6964b31c75ab4810cbbb9fd872edc63659f1cc`.

A 691-byte original Disturbed summary was prepared from the band's
[discography](https://disturbed1.com/music/) and its label's
[release information](https://media.rhino.com/index.php/press-release/sickness-25th-anniversary-edition).
The existing-artist 60-second refresh closed mode/stream cleanly but received
zero biography requests/ACKs. All six gallery assets were requested and
acknowledged in that run; peak concurrency was seven with no capacity refusals.
The command correctly reported failure to deliver the requested biography.
Caching of the previously served empty biography is a plausible explanation,
not a proven cache invalidation contract. No music objects, artist identities
or covers were changed. Evidence: `disturbed-roles/biography-refresh.txt` and
local biography/provenance files. Visible text remains unverified; a fresh
artist import with the biography present from the start is the next candidate
test, not an automatically authorized deletion of the existing artist.

### October 19 user-authorized biography fresh import

After the empty-biography refresh did not fetch text, the user explicitly asked
to remove the album and artist and retry. A read-only proof revalidated the
complete sixteen-track Disturbed graph, audio bytes, cover, references and exact
253-object inventory. A scoped, journal-derived cleanup then removed precisely
twenty owned objects: sixteen tracks, their abstract album/artist and two media
folders. Before each deletion it checked exact inventory, ownership, incoming
collection/artist links and folder children. The shared Music root and all 233
baseline objects were preserved. One `0x9108` handoff succeeded after restoration
of that baseline. No local files were deleted. Sixteen full-proof/mock checks
and independent review passed. Evidence: `biography-cleanup-inspect.txt`,
`biography-album-cleanup.txt` and the private exclusive cleanup journal.

Fresh-import preflight then confirmed artist and album absence. The new attempt
uses the same catalog routing GUID and original sixteen local MP3s, all six
gallery images, separate background/thumbnail and the prepared biography from
the beginning. This is a user-authorized experiment, not an automatic cache
replacement policy. Fresh-creation quiet completion now also waits for the
configured gallery when a biography is supplied; existing-artist text-only
refresh still permits already-cached artwork. The lifecycle suite passes 90
checks and the exact rebuilt CLI passes 43 refusal checks. Tool SHA256:
`6011422bc7bac67d8dc8e43fe37db2c202b3a44b8e6a669faf2a77e08afcc37e`.

The first fresh-import connection received old PPP/IP bytes before a CLIENT
greeting and stopped cleanly with `unexpected-client-signal`. No album object
creation occurred. A separate fresh session, with a new exclusive journal and
repeated absence checks, completed handshake and uploaded all sixteen tracks,
album links and cover. Artist handle 134221019 and album 100666596 replace the
explicitly removed test objects; the catalog GUID remains unchanged.

During the bounded 240-second observation, the HD made 77 image requests and
acknowledged all six gallery members (74 completed responses in total), but
requested neither the biography nor a new artist overview/background. There
were no capacity refusals or exhausted outbound budget. The responder closed
mode/stream cleanly and reported failure to deliver the biography, preserving
all uploaded objects and journal. This disproves the assumption that this
particular explicit removal/recreation sequence necessarily refreshes the
biography. Cached metadata surviving the sequence remains a hypothesis; do not
claim a working text refresh or repeat destructive recreation blindly.
Evidence: `biography-album-transfer.txt` for the aborted handshake and
`biography-album-retry.txt` for the new import and completed observation.

The separate final-proof/handoff session then succeeded: all sixteen payloads
matched the 159,318,487 local bytes, ordered references and cover were exact,
the 233-object baseline plus twenty intended new objects was intact, and one
`0x9108` handoff was confirmed. The album is restored; biography delivery remains
unproven. Evidence: `biography-album-final-handoff.txt` and its private proof
journal. A physical full power cycle, rather than another automatic removal,
is the next proposed refresh test.

### October 19 biography delivery after user deletion and restart

The user manually deleted the test album from the HD and restarted it. Fresh
read-only inventory contained 233 objects with none of the twenty previous test
handles. No additional cleanup was needed. Preflight confirmed artist/album
absence, then the original sixteen MP3s were imported with the same catalog
GUID, all eight image assets and the 691-byte biography available from the start.

Unlike the prior session, the HD requested model/artist overview, background,
thumbnail, biography and gallery routes. The bounded session recorded 27 HTTP
requests, four DNS queries, thirteen acknowledged image responses covering all
eight distinct assets, and **two biography requests with two complete ACKs**.
All six gallery members were acknowledged; peak concurrent flows were seven,
with zero capacity refusals or timeouts. Three requests were rejected; the
diagnostic fwlink response remains only an HTML approximation, not secure-clock
support. Mode/stream closed cleanly with 689 of 8192 outbound sends used.
This is the first nonempty biography delivery gate. Visible text and retention
still require the user's device check. The observed sequence does not establish
which deletion/restart step invalidates the cache or justify automatic deletion.

The immediate final album verification reported failure after logging all sixteen
disc normalizations, without identifying the failing stage. Those log lines do
not prove completion of the last payload check. No handoff success
was claimed. A separate read-only `normal-final-proof` then passed exact payload,
ordered references, cover, foreign-link and 233-plus-20 inventory checks. The
same reviewed helper repeated that proof and confirmed one explicit `0x9108`
handoff with a new exclusive journal. The immediate failure's cause remains
unconfirmed; no relaxed check or new cleanup was used. Final artist handle is
134221066, album 100666644, folders 83889416/83889417 and tracks
16780565 through 16780580. Original source files and all baseline objects remain.

Evidence under `disturbed-roles/`: `manual-reboot-inventory.txt`,
`biography-after-user-reboot-preflight.txt`, `biography-after-user-reboot.txt`,
`biography-reboot-proof.txt` and `biography-reboot-handoff.txt`, with exclusive
private creation and handoff journals. No production app feature was enabled.

The user subsequently confirmed “That worked” after checking the delivered
biography. Visible biography acceptance now passes for this HD experiment.
This does not establish persistence after another reboot, automatic discovery
or safe cache replacement for an existing artist.

### Remaining acceptance work

1. Trace the firmware cache-path routine to its writer and any USB/MTP caller.
   Establish a complete write contract before claiming direct push: destination,
   payload format, exact artist mapping, completion and readback. Keep analysis
   offline until there is a concrete operation to test.
2. Existing evidence does not establish that fetches can never occur: a full
   six-minute prebound run and an interrupted network-ready update run produced
   none. Further idle polling without a changed hypothesis was tabled at the
   user's request. The newly authorized fresh-artist experiment above changes
   the creation sequence. Reference artist recreation is not permission to
   replace existing artists or erase media.
3. The local-image delivery gate now passes for the fresh isolated artist above.
   Next verify visible Now Playing presentation, persistence and reconnect
   behavior on hardware. Accepted bytes alone cannot pass those gates. A direct
   cache-write mechanism is not a prerequisite for the demonstrated USB retrieval.
4. Integrate native app identity/artwork selection after the one-image gate.
   Do not change existing album covers, device artist identities or local files
   as a shortcut to an unverified background-art path.

### October 19 public C API extraction

The proven packet/catalog implementation was moved to shared `src/hd/` modules
with forwarding headers preserving the research tool entry points. The supported
C surface is implemented by `src/hd_artwork.c`; see
[HD_ARTWORK_API.md](HD_ARTWORK_API.md). It adds software-only full validation,
a serialized READY callback, bounded observation, per-asset and biography ACK
results, explicit no-fetch/partial outcomes, and safe teardown after uncertain
transport. Fresh artist creation preserves the verified Artists root and tagged
property-list shape. Existing unbound/conflicting identities are refused without
mutation. Strict album inventory avoids treating unreadable identities as empty.

The public API suite passes 82 ASan/UBSan checks, including complete mocked
initialization/PPP/no-fetch closure and cancellation/uncertain callbacks. Shared
server/network/gallery/init checks pass 2533/188/90/193 respectively. The public
example and `zunetool hd-art-publish --validate` build and validate prepared
content without opening USB. No physical-device operation was performed for this
extraction. Fresh application sync ordering, multiple artists, persistence and
existing-artist refresh remain hardware gates. The guide records review targets
for the user's upcoming libzune hardening work.
