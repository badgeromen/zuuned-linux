# Wire Capture Findings — Windows 8 Zune Client Ground Truth

> **Documentation status (2026-10-04):** Historical hardware/capture evidence. Preserve the session-specific findings below; current API and portability contracts are in API_REFERENCE.md and README.md. Capture files and machine-local paths mentioned here are not shipped as source dependencies.

**Session 2026-08-27.** All eight USBPcap captures (recorded while syncing with the
official Zune 4.8 software on Windows 8) were reconstructed into complete PTP/MTP
transaction logs — 52,000+ transactions decoded with <10 unparseable frames.
This doc records everything learned, ranked by impact on libzune correctness.

Capture inventory (Desktop/Downloads mirrors):

| Capture | Actual content |
|---|---|
| `USB-capture.pcapng` | **Firmware flash** (B000FF images + MSCF cab via SendObjectInfo/SendObject), then a second media-mode session |
| `capture-bulkreading.pcapng` | **Full USB sync**: 1,296 photo uploads (0x9808), 1,376 GetObject, 1,309 DeleteObject, ZMDB pulls |
| `episodesync.pcapng` | Mostly wireless (PPP tunnel); contains **one USB video upload** — the golden 0x9808 episode recipe |
| `album copy.pcapng` | Device→collection album copy: GetObject + SetObjPropList + SetObjectReferences |
| `photosync.pcapng`, `photocopy.pcapng`, `tvcopytocollection.pcapng`, `episodepull.pcapng` | Wireless-sync PPP tunnel sessions (see ZUNE_VENDOR_OPS.md PPP analysis) |

---

## 1. ZLP rule (fixes the #1 intermittent sync failure)

**The zero-length packet is decided by `payload_len % maxpacket`, NOT
`container_len % maxpacket`.**

Evidence: in `capture-bulkreading`, the single host→device ZLP among 1,296
uploads immediately follows a SendObject whose **payload is exactly 16,384
bytes** (512-multiple); the 12-byte data header traveled as its own URB.
`16384+12 = 16396 % 512 = 12` — the container-length test would have skipped it.
The device behaves symmetrically: every device→host ZLP follows a GetObject
payload that is a 512-multiple (82,432 / 81,920 / 93,184 observed).

libzune bug: `ptp.c` tests `total_container_len % maxpacket` in split mode.
Off by 12 → ~1 in 256 files hangs (missing ZLP) or desyncs (spurious ZLP),
purely by file size. **Fix: test `data_len % maxpacket` in the split path.**

## 2. Split header/data confirmed in BOTH directions

Every OUT data container in every capture ships its 12-byte header as a
separate URB before the payload — Windows always uses split mode host→device,
matching what libzune auto-detects for device→host.

## 3. Media objects are created with SendObjectPropList (0x9808), never SendObjectInfo

SendObjectInfo appears **only** in the firmware-flash capture. Every media
object — photos, videos, folders — is created with 0x9808 (all properties
atomic, before the data) followed by SendObject. Command params:
`[storageID, parentHandle, formatCode, sizeHigh, sizeLow]`.

libzune currently uses SendObjectInfo + five unchecked post-hoc
SetObjectPropValue calls for tracks. This architectural difference is the
likely root of "metadata didn't stick" symptoms (Unknown Artist, missing
titles). **Long-term fix: move smuggle paths to 0x9808.**

### Captured recipes (property order as sent)

**TV episode** — format `0xB216`, ~463 MB MP4, 11 props:

| # | Prop | Type | Example |
|---|---|---|---|
| 1 | 0xDC07 ObjectFileName | str | "Cyberpunk Edgerunners - S01E01 - Let You Down.mp4" |
| 2 | 0xDD62 (unknown) | u32 | 0 |
| 3 | 0xDA9A Series | str | "Cyberpunk Edgerunners" |
| 4 | 0xDC95 MetaGenre | u16 | 0x26 (TV) |
| 5 | 0xDAB5 Season | u32 | 1 |
| 6 | 0xDAB6 Episode | u32 | 1 |
| 7 | 0xDC9D DRMStatus | u16 | 0 |
| 8 | 0xDC44 Name | str | "Let You Down" |
| 9 | 0xDC89 Duration | u32 | 1534073 (ms) |
| 10 | 0xDC47 DateAuthored | str | "20161231T180000.0" |
| 11 | 0xDC48 Description | AUINT16 | empty array |

**Music track** — 12 props (observed via SetObjPropList 0x9806 during album
copy; creation via 0x9808 presumably identical):
FileName · **0xDAB0**=0 (u8) · MetaGenre=0x01 · **0xDAB2**=0 (u8) ·
DRMStatus=0 · Name · Duration · Rating(u16) · TrackNumber(u16) · Artist ·
Genre · DateAuthored.

**Album art** — format `0x3801` (JPEG), filename literally `ZuneAlbumArt.jpg`,
5 props: DateAuthored · FileName · **0xDA99 album-link GUID (u128)** ·
0xDC87 Width · 0xDC88 Height. (0xDA99 links the art object to its album —
previously unknown.)

**Folder** — format `0x3001`, single prop: ObjectFileName.

## 4. Storage: checked once per session, tracked via ZMDB

GetStorageInfo is called **once** at session setup — never polled per-send.
Live device state comes from re-pulling the ZMDB (`0x9217 [1]`) roughly every
55–60 s throughout the sync, with `0x9219 CheckChanges [0,0,5000]` as the
cheap "anything new?" probe. GetStorageInfo costs ~20 ms and is safe to call
between items — a good free-space guard for the Zuuned sync loop even though
Windows doesn't do it.

## 5. Played data lives in the ZMDB — nowhere else

Zero per-object reads of UseCount/play state in any capture. Play counts,
ratings, skip counts, last-played all ride in the ZMDB blob (already parsed by
libzune: playcount +26, rating varint 0x23, last_played varint 0x70). The only
Get/SetObjectPropValue traffic is `0xDC86`/`0xDC81` — representative-sample
album art, which libzune already implements.

## 6. Audiobooks are first-class

Media-mode DeviceInfo advertises the **`audible.com: 1.0`** MTP extension and
playback formats **`0xB904` (Audible)** and **`0xBA05` (AbstractAudioBook)**,
matching ZMDB record types 0x11 AudiobookTitle / 0x12 AudiobookTrack.
No audiobook transfer occurs in these captures — creation recipe unproven,
almost certainly the album pattern (0x9808 abstract object +
SetObjectReferences). One Windows audiobook-sync capture would close it.

## 7. Real client error posture and timing

- Eleven `0x2002 GeneralError` responses across the full sync (mostly
  PPP_Poll with param 0x8000FFFF, plus 0x9215): Windows **logs and moves to
  the next operation** — no retry of the failed op, no session teardown.
- SendObject → response latency **scales with payload**: ~0.5 s worst case
  for small files, **3.48 s for a 44.8 MB firmware payload**. Extrapolated to
  150 MB+ videos on an HDD Zune 30, libzune's fixed 10 s response-read
  timeout is in the danger zone. **Fix: use the 60 s data timeout for the
  response following a data phase.**
- GetObjPropList is read by **property group** (`groupCode` 2 = object
  basics, 4 = parent/storage) or `0xFFFFFFFF` (all props), not per-prop.

## 8. Newly observed, unmapped surface

Media-mode DeviceInfo advertises far more vendor ops than previously
documented: `0x9101–0x910B`, `0x9170–0x9172`, `0x9180–0x9185`, `0x9204`,
`0x9215–0x921D`, `0x9220–0x9232`, `0x9240–0x9243`. Newly seen in live use:
props `0xDD62`, `0xDAB0`, `0xDAB2`, `0xDA99` (art link GUID), `0xDC87/0xDC88`
(width/height). MetaGenre values confirmed: 0x01 music, 0x26 TV.

## Tooling

The reconstruction scripts (USBPcap TSV → PTP transaction log; ObjectPropList
decoder) live with the 2026-08-27 analysis session and are re-runnable against
any future capture:
`tshark -Y 'usb.transfer_type == 3' -T fields -e frame.number -e frame.time_relative -e usb.endpoint_address -e usb.irp_info -e usb.data_len -e usb.capdata`
then reassemble containers per direction (OUT = irp_info 0x00, IN = 0x01),
honoring split headers and 65,535-byte capture truncation.
