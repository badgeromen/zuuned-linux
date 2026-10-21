# Zune vendor opcodes — decoded from Windows client captures

> **Documentation status (2026-10-04):** Dated capture research. For later corrections, including SyncNotify percentages/direction/framing, read WIRE_CAPTURE_FINDINGS.md and src/finalize.c. The PPP section was re-audited on 2026-10-17; see [HD artwork research](HD_ARTWORK_RESEARCH.md) for exact frames, corrected lengths and application-level limits. Parent-application plans and capture files referenced below are not shipped in this standalone library.

Reverse-engineered from `~/Downloads/*.pcapng` Wireshark captures of the
official Microsoft Zune software talking to real Zune hardware. This file
is the durable record of what each vendor opcode does on the wire so that
libzune can implement (or consciously ignore) each one.

See also:
- `tools/ptp-decode.py` — helper to dump a hex PTP container
- `docs/ZUNE_MTP_PROTOCOL_FINDINGS.md` — higher-level notes about Zune MTP quirks
- `docs/FAST_DEVICE_READS_PLAN.md` (parent repo) — execution plan this decode feeds into

## Key findings (2026-04-11)

1. **0x922C and 0x922D carry a PPP-over-USB network tunnel.**
   Captures prove LCP/IPCP/CCP negotiation and IPv4 traffic alongside MTP.
   0x922B and 0x9230 accompany setup/control; their complete semantics are
   unverified. The fixed 0x922F response has not been shown to carry PPP
   frames. File transfers work without this tunnel. Capture interleaving
   does not establish the official client's threading implementation.

2. **0x9217 is `GetZMDB`** — downloads the device's entire Zune Media
   Database as a PTP data phase. The official client reads the ZMDB first,
   then uses it to decide what to sync. NOT MTPZ-related as previously
   assumed.

3. **0x922A is `SyncNotify`** — sends a track/file name to the device
   before each operation (1:1 with GetObject/SendObject). It's a prefetch
   hint or sync progress notification, NOT a batch metadata query. The
   payload includes progress counters (item index, total items).

4. **No "batch metadata query" exists.** The original plan hypothesized
   that vendor opcodes would replace per-object metadata loops. The
   captures show the official client also uses standard MTP ops
   (GetObjPropList, GetObjPropValue, SetObjPropValue) for metadata.
   Performance gains come from chunk size (Session 1) and ZMDB-first
   workflow, not from batch vendor ops.

5. **The captured Zune families use different bulk OUT endpoints** (0x01 vs
   0x02). The old DEXT hardcoding concern was platform-specific; current
   transports must discover endpoints from descriptors.

## Captures analyzed

| File | Size | Pkts | Dur | Key findings |
|---|---|---|---|---|
| `photocopy.pcapng` | 32 MB | 4561 | 50 s | 0x922D polling is independent of file chunks; 0x9217 returns ZMDB; 0x922A sends track name; only 1 GetObject for 128 MB file |
| `album copy.pcapng` | 109 MB | 14k | 94 s | 0x922A 1:1 with GetObject; decoded payload header (progress counters + UCS-2 name); full per-file sequence mapped |
| `tvcopytocollection.pcapng` | 44 MB | 14k | 113 s | PPP tunnel decoded: 0x922C sends "CLIENTSERVER" init, then HDLC-framed LCP/IPCP; confirmed 0x922D carries PPP data IN |
| `episodesync.pcapng` | 116 MB | 10k | 64 s | Upload direction confirms same pattern — SyncNotify + SendObject |
| `episodepull.pcapng` | 6 MB | 2190 | 20 s | Endpoint 0x01 device — same protocol, different endpoint |
| `photosync.pcapng` | 252 KB | 1572 | 23 s | Endpoint 0x01 device — photo upload, same protocol |
| `capture-bulkreading.pcapng` | 181 MB | 146k | 797 s | Full-scale sync: ~2672 SyncNotify + 1376 GetObject + ~1296 SendObject + 1309 Delete |

**Two Zune models appear in the captures, using different bulk OUT endpoints.**
`photosync.pcapng`, `episodepull.pcapng` and `albumsync.pcapng` enumerate
Zune HD VID/PID `045e:063e` and use bulk OUT endpoint `0x01`; the other
seven captures in the October audit enumerate `045e:0710`. The historical
DEXT hardcoded-endpoint concern does not describe the current Linux libusb
backend; endpoints must come from the device descriptors.

## Opcode inventory

| Opcode | Name | Calls in captures | Wire format known? | Purpose known? |
|---|---|---|---|---|
| `0x9108` | `ZUNE_CleanDataStore` | low | ✅ | ✅ finalize path |
| `0x9171` | `ZUNE_0x9171` | low | ✅ | ✅ finalize path |
| `0x9201` | `ZUNE_ReportAddedDeleted` | low | ✅ | ✅ finalize path |
| `0x9202` | `ZUNE_ReportAcquired` | low | ✅ | ✅ finalize path |
| `0x9212` | `ZUNE_MTPZ_SendCertificate` | low | ✅ | ✅ MTPZ handshake |
| `0x9213` | `ZUNE_MTPZ_ValidateResponse` | low | ✅ | ✅ MTPZ handshake |
| `0x9214` | `ZUNE_MTPZ_EnableTrusted` | low | ✅ | ✅ MTPZ handshake |
| `0x9215` | `ZUNE_0x9215` | ~20 | ❌ | ❌ MTPZ-adjacent? |
| **`0x9217`** | **`ZUNE_GetZMDB`** | low | ✅ | ✅ **downloads entire ZMDB** |
| **`0x9219`** | **`ZUNE_CheckChanges`** | ~20 | ✅ | 🤔 hypothesis |
| **`0x922A`** | **`ZUNE_SyncNotify`** | ~6000 (overcounted) | ✅ | ✅ **drives on-device sync progress display (working recipe below)** |
| `0x922B` | `ZUNE_PPP_Control` (working name) | historical estimate ~28 | partial | setup/control association; 260-byte or empty payload observed |
| **`0x922C`** | **`ZUNE_PPP_Send`** | 126 commands in October audit | ✅ | ✅ **send PPP frames to device** |
| **`0x922D`** | **`ZUNE_PPP_Poll`** | 10,299 commands in October audit | ✅ | ✅ **receive PPP frames; polling interval varies** |
| **`0x922F`** | **`ZUNE_0x922F`** | 208 commands in October audit | fixed response observed | unknown; bulk PPP fetch claim not supported |
| `0x9230` | `ZUNE_PPP_Mode` (working name) | historical estimate ~14 | partial | paired with 0x922B; exact semantics unverified |

---

## 0x9217 — `ZUNE_GetZMDB` (CONFIRMED)

**Status:** ✅ wire format and purpose confirmed from `photocopy.pcapng`.

**NOT MTPZ-adjacent** as previously hypothesized. This opcode downloads the
device's entire Zune Media Database (ZMDB) binary blob.

### Wire format

**Command (bulk OUT, type `0x0001`) — 16 bytes:**

```
[0:4]   length = 0x00000010 (16 bytes)     uint32 LE
[4:6]   type   = 0x0001 (command)          uint16 LE
[6:8]   opcode = 0x9217                    uint16 LE
[8:12]  txid                               uint32 LE
[12:16] param1 = 0x00000001                uint32 LE  (possibly "full DB" vs "delta")
```

**Data phase IN (bulk IN) — variable, ~264 KB observed:**

Split-header mode: 12-byte header (`length | 0x0002 | 0x9217 | txid`)
followed by data payload. Data starts with ZMDB magic bytes `5a4d4442`
("ZMDB"), followed by `01000000` (version 1), then the full ZMDB
structure: `ZMed` sections, `ZArr` arrays, media records, etc.

The data payload also contained UCS-2 encoded strings (filenames,
titles, artist names) — confirming this is the full media library DB.

**Response:** standard 12-byte OK. When the payload is a multiple of 512
bytes the device first ends the data phase with a zero-length packet (a Zune 80
with a 414,720-byte database, 2026-10-19); the host must read past it to the
response, or the next transaction receives this OK as its own answer.

### Observed in photocopy.pcapng

- Called once, early in the session (frame 215, t=28.88s), before any
  file transfers begin.
- Returned ~264 KB of ZMDB data across one 256 KB bulk IN + one 496-byte
  tail chunk.
- The official client reads the ZMDB first, then uses it to decide what
  to sync — rather than walking the MTP object tree like we do.

### Implications for libzune

We already have `zmdb_usb_read()` at `zmdb.c:582` that does raw bulk
reads. This opcode is likely what should be used instead — it's the
proper PTP-framed way to fetch the database. The raw bulk path may be
a legacy workaround.

### ZMDB content findings (from live device testing 2026-04-15)

The ZMDB returned by this opcode contains full media library data with
record types for music, video, photo, album, artist, genre, playlist,
and photo album. Key findings from parsing real device data:

**Video records (schema 0x02):** 40-byte fixed portion + variable title.
The display title is stored at offset +40 as UTF-8 — NOT from reference
fields. `filenameRef` at +12 is always 0, `titleRef` at +4 resolves
empty. The title contains the MTP Name property value set during upload
(e.g. "Blade Runner.wmv", "SAO Abridged - S01E01"). MetaGenre equivalent
at +38 correctly identifies movies (0x0002), TV shows (0x0004), and
music videos (0x0001). Format code at +32 identifies WMV (0xB981) vs
MP4 (0xB982). Confirmed on Zune 30 and Zune HD.

**Music records (schema 0x01):** Fixed portion (28 Classic / 32 HD) +
variable title. HD records include playcount (+26), codec_id (+28),
and rating (+30) at fixed offsets. Optional backwards varint fields
after the title string encode skip_count (0x63), disc_number (0x6C),
and last_played timestamp (0x70).

**Picture records (schema 0x03):** 24-byte fixed + variable title.
Offset +16 is a FILETIME timestamp (uint64), NOT width/height as
previously hypothesized. Photo dimensions are NOT in the ZMDB.

See `docs/ZMDB_FORMAT.md` for complete record layouts.

---

## 0x9219 — `ZUNE_CheckChanges` (hypothesis)

**Status:** wire format decoded, purpose hypothesis.

### Wire format

**Command (bulk OUT, type `0x0001`) — 24 bytes:**

```
[0:4]   length = 0x00000018 (24 bytes)     uint32 LE
[4:6]   type   = 0x0001 (command)          uint16 LE
[6:8]   opcode = 0x9219                    uint16 LE
[8:12]  txid                               uint32 LE
[12:16] param1 = 0x00000000                uint32 LE
[16:20] param2 = 0x00000000                uint32 LE
[20:24] param3 = 0x00001388 (5000)         uint32 LE  (timeout? max count?)
```

**Data phase IN — 4 bytes:**

Split-header: 12-byte header, then 4 bytes `00 00 00 00`.

**Response — 24 bytes:**

```
rparam1 = 0x00000000
rparam2 = 0x00000000
rparam3 = 0x00000000
```

### Hypothesis

Called once, immediately after MTPZ handshake (0x9214) and before 0x922A.
The `5000` third param could be a timeout (ms) or max-items-to-return.
The all-zero response + all-zero data suggests "no changes since last
sync." Possibly a delta/changelog query the client uses to decide
whether a full ZMDB re-read is needed.

---

## 0x922A — `ZUNE_SyncNotify` (CONFIRMED)

**Status:** ✅ wire format confirmed from `photocopy.pcapng` and
`album copy.pcapng`. Purpose: **pre-operation sync notification — sends
the item name and progress counters to the device before each file op.**

**CORRECTION:** Previous hypothesis called this a "batch metadata query."
It is NOT a batch query — it notifies the device about an upcoming operation.

### Wire format

**Command (bulk OUT, type `0x0001`) — 12 bytes:**

```
[0:4]   length = 0x0000000C (12 bytes)     uint32 LE
[4:6]   type   = 0x0001 (command)          uint16 LE
[6:8]   opcode = 0x922A                    uint16 LE
[8:12]  txid                               uint32 LE
```

No parameters in the command phase.

**Data phase OUT (bulk OUT, type `0x0002`) — 542 bytes observed:**

Split-header: 12-byte header (`length | 0x0002 | 0x922A | txid`),
then 512-byte payload, then 18-byte tail (zero-padded).

Payload structure (from photocopy.pcapng txid 0x1e80):

```
[0:4]   0x00000001              uint32 LE  (count? version? flag?)
[4:8]   0x00000000              uint32 LE  (padding or type)
[8:12]  0x00000032 (50)         uint32 LE  (string length in chars? or max?)
[12:16] 0x00000000              uint32 LE  (padding)
[16:20] 0x00000002              uint32 LE  (flag — possibly "match type")
[20:]   UCS-2LE encoded string, null-terminated, zero-padded to fill
```

Decoded string: **`"Cyberpunk Edgerunners - S01E01"`**

The remaining bytes after the string are zero-padding to a fixed 512-byte
payload size.

**Response:** standard 12-byte OK (no data phase IN).

### Observed in photocopy.pcapng

- Called 4 times (not ~6000 — mixed capture inflated prior count).
- Called right before `GetObject (0x1009)`.
- The name matches content visible in the ZMDB dump from 0x9217.

### Observed in album copy.pcapng — full per-file sequence

56 × 0x922A, 1:1 with 56 × GetObject. (Prior "112" count was wrong —
double-counted CMD + DATA headers since both are 12-byte bulk OUT with
the opcode at the same position.)

Each file copy follows this exact sequence:

```
1. NameQuery (0x922A)  — send track name to device
2. GetObjPropList × 2  — read metadata (pre-download)
3. GetObject           — download file content
4. GetObjPropList × 2  — read metadata (post-download)
5. GetObjPropValue × 2 — individual prop reads
6. → next NameQuery
```

Track names observed: Subnautica soundtrack — "Original Inhabitants",
"In Bloom", "Sun & Moon", "First Immersion", "Shallows", etc.

### Data payload header structure (revised x2)

Second revision after decoding all 2,672 notifies in
`capture-bulkreading.pcapng` (a real host→device sync: 1,296 photo
uploads via 0x9808, 1:1 with 0x922A). The earlier "always 1" reading
came from device→host copy captures only.

```
[0:4]   op_kind                 uint32 LE  (0 = host→device WRITE — every
                                            upload in bulkreading uses 0;
                                            1 = device→host read, the value
                                            seen in the copy captures)
[4:8]   progress_before         uint32 LE  (PERCENT 0-100 of whole batch)
[8:12]  progress_after          uint32 LE  (PERCENT 0-100 of whole batch)
[12:16] item_index              uint32 LE  (1-based sequential index in batch)
[16:20] total_items             uint32 LE  (ALL work items in the session —
                                            4,480 in bulkreading, spanning
                                            deletes + uploads + reads)
[20:]   UCS-2LE string, null-terminated, zero-padded to 492 bytes
```

Observed percent pairs: `0→50` (2-item batches), `0→7` (13 items),
`77→77` mid-way through the 4,480-item span — small per-item steps
truncate to a static percent on huge batches. The old "work units with
delta 1 or 2" reading was these same percentages misread on small
batches.

### ✅ WORKING RECIPE — on-device sync progress display (2026-08-29)

Confirmed on real hardware: with the recipe below, the Zune's own
screen shows the sync progress (item name + percentage) during a
host→device sync. Verified on **Keel (Zune 30)**; implementation is
`zune_sync_notify()` in `src/finalize.c`, driven per-item by the app's
sync engine.

Send one 0x922A immediately BEFORE each file transfer, in the same
MTP session:

1. **Command** (12 bytes, no params): `len=12 | type=0x0001 | 0x922A | txid`.
2. **Data phase OUT, split-header mode**: the 12-byte data header
   (`len=542 | type=0x0002 | 0x922A | txid`) travels as its own USB
   write, then the **530-byte payload** as a second write.
   Payload = 512-byte body + 18-byte zero tail (matches the official
   client's 542-byte container exactly).
3. **Body fields** (all uint32 LE, name UCS-2LE at +20,
   null-terminated, remainder zero):
   - `op_kind = 0` (host→device write)
   - `progress_before` / `progress_after` = whole-batch **percent**
     (0-100): `idx*100/total` → `(idx+1)*100/total`
   - `item_index` = **1-based** position in the batch
   - `total_items` = batch size
4. Response is a plain OK; there is no data phase IN. Best-effort —
   a refusal must not fail the transfer.

Three failure modes found on the way here (each looked "almost right"):

| Attempt | Result |
|---|---|
| 512-byte payload | **Refused**, RC 0x2002 on every call — firmware checks exact framing; must be 530 bytes (542-byte container) |
| 530 bytes, `op_kind=1` | **ACKed but ignored** — screen never updates; 1 is the read-direction value from device→host copy captures |
| 530 bytes, `op_kind=0`, percent + 1-based index | **Works** — device screen shows name + progress |

How the semantics were recovered: decoded ALL 2,672 notifies in
`capture-bulkreading.pcapng` (the one capture that is a true
host→device sync — 1,296 photo uploads, 0x922A 1:1 with 0x9808) and
diffed field values against the device→host copy captures the original
hypothesis was built on. Write-direction rows carried `0` in the first
field and slow-moving percents; read-direction rows carried `1`.

### Purpose

**Sync progress notification / prefetch hint.** The client tells the
device "I'm about to work with this item" before each file operation —
this is what drives the device's on-screen sync status. The device
does NOT return data — just acknowledges.

Not required for file transfers to work (our current code omits it and
transfers succeed). However, on HDD-based Zunes, this could serve as a
seek hint — the device firmware could start positioning the HDD head
while the host is still reading metadata. Omitting it might explain
some of the slowness we observe on HDD Zunes.

---

## 0x922C / 0x922D: PPP transport; 0x922F: unresolved status dataset

**Re-audited 2026-10-17.** Captures establish PPP framing and network
negotiation over the MTP bulk endpoints. They do not establish the complete
artist-catalog protocol or image-serving contract. Exact capture hashes and
frame evidence are in [HD artwork research](HD_ARTWORK_RESEARCH.md).

| Opcode | Observed role | Direction |
| --- | --- | --- |
| `0x922C` | Send initialization text or HDLC-framed PPP | Host to device, data OUT |
| `0x922D` | Receive initialization text or HDLC-framed PPP when available | Device to host, optional data IN |
| `0x922F` | Read fixed dataset; semantics unresolved | Device to host, data IN |

### Protocol sequence from tvcopytocollection.pcapng

The device sends `CLIENT` through `0x922D` at DATA frames 547 and 837.
The host responds with `CLIENTSERVER`: command 1296, DATA header 1298,
payload 1300 and response 1303. Parameters are `(3, 3)`; the response's
fourth parameter is 12, matching the initialization payload length. Treat
"stream 3" as a working interpretation of these parameters, not a decoded
stream-selection contract.

LCP then proceeds through an incoming Configure-Request (1417), outgoing
Configure-Ack and Configure-Request (1436), and incoming Configure-Ack plus
IPCP Configure-Request (1445). DATA frame numbers identify the container
header; payload may arrive in the following USB transfer.

LCP options are **ACCM=0**, Protocol-Field-Compression and ACFC. The earlier
MRU=0 label was incorrect: option `02`, length 6, is ACCM, whereas MRU
would be option `01`.

The initial IPCP request asks for IP, primary/secondary DNS and NBNS
assignments with zero addresses. Frames 1470/1479/1504/1513 include further
IPCP and CCP negotiation. IPv4 follows: DHCP Inform appears in
1513/1597/1685, and TCP SYN/SYN-ACK/ACK in 1767/1786/1797. That TCP
handshake carries no application payload. The sequence repeats four times
in this capture. This is network transport evidence, not HTTP artwork evidence.

### Wire formats

**0x922C:**
- Command: 20 bytes, parameters `(3, 3)` in examined exchanges.
- Data: 12-byte PTP header plus initialization text or HDLC-framed PPP.
- Response: 28 bytes; fourth response parameter reports payload length.

**0x922D:**
- Command: 20 bytes, parameters `(3, 3)`.
- Idle: 24-byte response, with parameters `(0, 3, 0)` in observed examples.
- Active: 12-byte DATA header plus initialization text or HDLC-framed PPP,
  then a separate 24-byte response.

**0x922F:**
- Command: 12 bytes, no parameters.
- DATA container: **1048 bytes total = 12-byte PTP header + 1036-byte payload**.
- First 16 payload bytes are four little-endian uint32 values. Observed
  combinations: `(1,0,0,0)`, `(1,0,1,1)`, `(1,100,4,3)`, `(1,0,3,2)`.
- The remaining **1020 bytes are zero in all 208 recovered datasets**.
- No PPP frames were observed there. A status structure is a hypothesis;
  the former name "bulk fetch PPP buffer" and claimed nonzero PPP content
  must not drive an implementation.

### Transaction counts and timing

Counts below include command containers only, not DATA or response headers.
The October audit covered ten captures; `USB-capture.pcapng` had no selected
PPP operations.

| Capture | 0x922C | 0x922D | 0x922F |
| --- | ---: | ---: | ---: |
| `photosync.pcapng` | 4 | 288 | 8 |
| `photocopy.pcapng` | 9 | 650 | 15 |
| `album copy.pcapng` | 15 | 1242 | 20 |
| `albumsync.pcapng` | 7 | 664 | 12 |
| `tvcopytocollection.pcapng` | 20 | 1701 | 37 |
| `episodesync.pcapng` | 16 | 1307 | 25 |
| `episodepull.pcapng` | 5 | 357 | 8 |
| `newzune.pcapng` | 15 | 1222 | 24 |
| `capture-bulkreading.pcapng` | 35 | 2868 | 59 |

Polling is not uniformly 1 Hz: commands 486 and 490 in
`tvcopytocollection.pcapng` occur at 24.117495 and 24.130485 seconds,
about 13 ms apart. The captures do not justify a fixed poll interval or
an assumption that every few polls cause a bulk fetch.

### Implications for libzune

File transfers already work without PPP. A future tunnel must share the
serialized device owner with other MTP work. Capture interleaving is not
permission for concurrent USB transactions.

HD artist artwork is now an active private research workstream. Existing
captures show no catalog HTTP request or `deviceBackgroundImage` request
inside PPP. The HD `albumsync` capture contains four 40-byte TCP DNS proxy
messages: `stats.zune.net` queries and NXDOMAIN replies. Native PPP/IP and HTTP
now pass on hardware, but image requests remain unobserved. The live evidence,
reversible artist-binding experiment and next physical gates are documented in
[HD artwork research](HD_ARTWORK_RESEARCH.md).

---

## Decoding workflow

`tools/ptp-decode.py` is the helper. Invoke with a hex string:

```sh
tools/ptp-decode.py 14000000 0100 2d92 01000000 03000000 03000000
```

To pull a specific transaction's phases from a capture:

```sh
CAP=~/Downloads/photosync.pcapng
TXID="01000000"  # target transaction ID in hex LE

# Find bulk OUT frames (endpoint depends on device — 0x01 or 0x02)
tshark -r "$CAP" \
  -Y "usb.device_address == 2 && usb.endpoint_address == 0x01 && usb.transfer_type == 0x03" \
  -T fields -e frame.time_relative -e usb.data_len -e usb.capdata 2>/dev/null \
  | grep "$TXID"

# And bulk IN frames
tshark -r "$CAP" \
  -Y "usb.device_address == 2 && usb.endpoint_address == 0x81 && usb.transfer_type == 0x03" \
  -T fields -e frame.time_relative -e usb.data_len -e usb.capdata 2>/dev/null \
  | grep "$TXID"
```

Feed each `usb.capdata` value to `ptp-decode.py` to parse the container.
