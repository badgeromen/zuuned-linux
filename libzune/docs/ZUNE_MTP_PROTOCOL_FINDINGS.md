# Zune MTP Protocol — Reverse Engineering Findings

> **Documentation status (2026-10-04):** Historical April capture/test findings. Later wire evidence supersedes the container-length ZLP rule below: split-mode ZLP uses PAYLOAD length and excludes zero-length payloads. Current transfers use larger chunks. Read WIRE_CAPTURE_FINDINGS.md and the linked current implementation in TOC.md before changing transport behavior.

> Documented from live testing with Zune 80 (PID 0x0710) via DriverKit DEXT.
> Updated: 2026-04-08

---

## 1. Split Header/Data Mode (Critical)

The Zune requires PTP data containers to be sent as **two separate USB bulk transfers**: a 12-byte header first, then the payload. Without this, all MTP operations return `0x2002` (General Error).

**Detection:** During the first `GetDeviceInfo` (0x1001), the Zune sends the data container header (12 bytes) separately from the payload. If `first_read_actual == 12 && container_length > 12`, enable split mode.

**Implementation:** `ptp.c:ptp_send_data()` — when `s->split_header_data == 1`:
1. Write 12-byte header: `[length][type=DATA][code][transaction_id]`
2. Write payload in CONTEXT_BLOCK_SIZE chunks (64KB), aligned to maxpacket (512)
3. Send ZLP if `total_container_length % maxpacket == 0`

**Reference:** Vendored libmtp detects this identically at `libusb1-glue.c:1526`.

---

## 2. USB Endpoint Configuration

| Endpoint | Address | MaxPacket | Direction |
|----------|---------|-----------|-----------|
| Bulk IN  | 0x81    | 512       | Device → Host |
| Bulk OUT | 0x02    | 512       | Host → Device |
| Interrupt| 0x83    | —         | Events |

Interface: class=0x00, 3 endpoints. Note: NOT class 0xFF like some MTP devices.

---

## 3. MTPZ Authentication

Required before any write operations. Sequence:

1. `GetDeviceInfo` (0x1001) — BEFORE MTPZ (needed to detect split mode)
2. `SetSessionInitiatorInfo` (0x1016) — UCS-2 encoded initiator string
3. `EndTrustedAppSession` (0x9692) — Reset handshake
4. `SendCertificate` (0x9212) — 785-byte RSA certificate
5. `ValidateResponse` (0x9213) — Device validates
6. `SendConfirmation` (0x9212) — 32-byte confirmation
7. `EnableTrustedFilesOperations` (0x9214) — 16-byte session nonce
8. `OpenSession` (0x1002) — Standard PTP session

Keys: Embedded in `mtpz_keys.h`, overridable via `~/.mtpz-data`.

---

## 4. Track Send Flow

### Working approach: SendObjectInfo (0x100C) + SendObject (0x100D) + SetObjectPropValue

```
SendObjectInfo (0x100C)  →  Creates object with basic info (format, size, filename)
SendObject (0x100D)      →  Send file data
SetObjectPropValue       →  Set metadata one property at a time:
  0xDC44 (Name)          →  OK ✓
  0xDC46 (Artist)        →  OK ✓
  0xDC8C (Genre)         →  OK ✓
  0xDC8B (Track)         →  OK ✓
  0xDC89 (Duration)      →  OK ✓
  0xDC9A (AlbumName)     →  FAIL 0x200F (Access Denied — read-only post-creation)
  0xDC9B (AlbumArtist)   →  FAIL 0x200F (Access Denied — read-only post-creation)
```

**Key finding:** AlbumName and AlbumArtist are read-only after object creation. They can ONLY be set atomically via SendObjectPropList (0x9808) during creation. However, the Zune displays album/artist info from album objects, not track properties — so skipping these is fine.

### Properties settable post-creation on audio tracks:
| Property | Code | Type | Status |
|----------|------|------|--------|
| Name (Title) | 0xDC44 | STR | OK |
| Artist | 0xDC46 | STR | OK |
| Genre | 0xDC8C | STR | OK |
| Track Number | 0xDC8B | UINT16 | OK |
| Duration | 0xDC89 | UINT32 | OK |
| AlbumName | 0xDC9A | STR | **DENIED** |
| AlbumArtist | 0xDC9B | STR | **DENIED** |

---

## 5. Album Object Creation (AbstractAudioAlbum — 0xBA03)

### Supported properties (queried via GetObjectPropsSupported 0x9801):

```
0xDA99  (Zune vendor — unknown)
0xDC84  RepresentativeSampleData
0xDC44  Name ✓
0xDC04  ObjectSize
0xDAB0  (Zune vendor — unknown)
0xDA97  (Zune vendor — unknown)
0xDC4F  NonConsumable
0xDC07  ObjectFileName ✓
0xDC02  ProtectionStatus
0xDC0B  ParentObject
0xDC81  RepresentativeSampleFormat
0xDC01  StorageID
0xDC41  PersistentUniqueObjectIdentifier
0xDC86  RepresentativeSampleSize
0xDC46  Artist ✓
0xDC83  RepresentativeSampleHeight
```

### NOT supported on albums:
| Property | Code | Status |
|----------|------|--------|
| AlbumArtist | 0xDC9B | **NOT IN SUPPORTED LIST** |
| Genre | 0xDC8C | **NOT IN SUPPORTED LIST** |
| Composer | 0xDC47 | **NOT IN SUPPORTED LIST** |

**Key finding:** The Zune uses `Artist` (0xDC46) on album objects, NOT `AlbumArtist` (0xDC9B). Sending AlbumArtist causes `0xA801` (ObjectPropNotSupported) and the entire creation fails.

### Working album creation via SendObjectPropList (0x9808):
```
Properties sent:
  ObjectFileName (0xDC07) — e.g. "Album Name.alb"
  Name (0xDC44) — album display name
  Artist (0xDC46) — artist name
```

After creation:
1. `SendObject` (0x100D) — 1 byte (abstract objects need minimal data)
2. `SetObjectReferences` (0x9811) — array of track object IDs
3. Album art via `SetObjectPropValue` for RepresentativeSampleData (0xDC86)

---

## 6. Artist Objects (AbstractArtist — 0xB218)

**Format 0xB218 is NOT supported** by the Zune via GetObjectPropsSupported (returns 0x2016). Artist objects should NOT be created separately.

The Zune derives artist information from:
- Track-level `Artist` property (0xDC46)
- Album-level `Artist` property (0xDC46)

No separate artist MTP objects needed.

---

## 7. USB Stall Recovery (DriverKit-specific)

**DO NOT call ClearEndpointStall (clear_halt) on DriverKit endpoints that aren't stalled.** This corrupts the pipe state and causes `0xE00002D6` (kIOReturnNotResponding) on ALL subsequent transfers.

**Vendored libmtp behavior:**
- NEVER calls clear_halt between transfers
- clear_halt only called during `close_usb()` (device disconnect)
- clear_halt is explicitly DISABLED (`#if 0`) in libusb-glue.c due to kernel bugs
- No pauses between successive SendObjectInfo → SendObject calls

**Our approach (macOS/DriverKit):**
- No clear_halt during active transfers
- 200ms pause between track sends (safety margin for DEXT overhead)
- Reactive unjam disabled on macOS (`#ifndef __APPLE__`)
- On Linux (libusb), reactive unjam in retry loops is safe

---

## 8. Zune Vendor Properties

| Property | Code | Type | Used For |
|----------|------|------|----------|
| MetaGenre | 0xDC8C (mapped) | UINT16 | 0x25=Movie, 0x26=TV, 0x23=MusicVideo |
| SeriesTitle | 0xDA9A | STR | TV series name |
| SeasonNumber | 0xDAB5 | UINT32 | TV season |
| EpisodeNumber | 0xDAB6 | UINT32 | TV episode |
| ArtistId | 0xDA97 | UINT32 | Links track → artist object |
| Unknown | 0xDA99 | — | Seen on album objects |
| Unknown | 0xDAB0 | — | Seen on album objects |

---

## 9. PTP Transaction Requirements

- Transaction ID for `OpenSession` (0x1002) MUST be 0 per PTP spec
- All other transactions increment normally
- Session ID = 1 (standard)
- Response containers are always < 512 bytes (short packets)

---

## 10. ZMDB (Zune Media Database)

Binary database returned via vendor operation. Contains device library:
- Tracks, videos, photos, albums, artists, playlists
- Record atoms identified by type + size prefix
- Used for dedup checking before sync

See `docs/ZMDB_FORMAT.md` and `docs/ZUNEDB_ARCHITECTURE.md` for full format spec.

---

## 11. DriverKit DEXT Findings (v13)

### OSData Ownership in IOUserClient
When returning data via `args->structureOutput = outData`, the DriverKit framework takes ownership of the OSData object with its existing refcount. **Do NOT call `outData->release()` after assignment** — this destroys the data before IPC can serialize it, causing `kIOReturnIPCError` (0xFFFFFECC).

### Pipe Abort on Stop
`IOUSBHostPipe::Abort()` requires 3 arguments: `Abort(options, withError, forClient)`. Call before releasing pipes in `Stop()` to prevent use-after-free when USB device is unplugged during active I/O:
```cpp
if (ivars->bulkInPipe) ivars->bulkInPipe->Abort(0, kIOReturnAborted, this);
```

### DEXT Process Death Under Sustained I/O
The DEXT process silently dies after 9-34 sustained bulk transfers (variable count). Symptoms: `Interrupted ... _connection ... _server 0x0` in app log. No error in DEXT logs. Root cause under investigation — not OSData leaks (framework handles ownership), not clear_halt (already removed). Possible causes: DriverKit process memory limits, IOUSBHostPipe internal state accumulation, or macOS xHCI driver behavior.

### Per-Call Buffer Allocation
Each `sBulkRead`/`sBulkWrite` creates a fresh `IOBufferMemoryDescriptor` per call. Persistent buffer reuse was attempted but caused race conditions on concurrent access. Per-call allocation is safer and the overhead is negligible relative to the USB transfer time.

---

## Testing Environment

- **Device:** Zune 80 (VID=0x045E, PID=0x0710)
- **USB:** DriverKit DEXT (ZuneUSBDriver v13), IOProbeScore 100000
- **Host:** macOS (Darwin 25.2.0, arm64)
- **Auth:** MTPZ via libgcrypt (RSA/AES)
- **Stack:** libzune native PTP/MTP (no vendored libmtp)
