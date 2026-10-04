# Zune Vendor MTP Properties & Operations

## Overview

The Microsoft Zune uses standard MTP (Media Transfer Protocol) with extensive vendor extensions. This document catalogs every property, operation, and format code specific to the Zune, discovered through `mtp-detect` dumps, protocol analysis, and reverse engineering.

## Device Identification

| Field | Value |
|-------|-------|
| Vendor ID | 0x045E (Microsoft) |
| Product ID (Classic) | 0x0710 |
| Product ID (Zune HD) | 0x063E |
| Vendor Extension | microsoft.com/WMDRMPD: 10.1; microsoft.com/WMPPD: 11.1; microsoft.com/MTPZ: 1.0 |

## Vendor Object Formats

| Code | Name | Description |
|------|------|-------------|
| 0x3009 | MP3 | Audio — MPEG Layer 3 |
| 0xB901 | WMA | Audio — Windows Media Audio |
| 0xB903 | AAC | Audio — Advanced Audio Coding |
| 0xB215 | M4A | Audio — MPEG-4 container |
| 0x300C | ASF | Video — Advanced Systems Format |
| 0xB981 | WMV | Video — Windows Media Video |
| 0x3801 | JPEG | Image — JPEG |
| 0x3001 | Association | Directory/Folder |
| 0xBA03 | AbstractAudioAlbum | Music album object |
| 0xBA05 | AbstractAudioVideoPlaylist | Playlist object |
| 0xB218 | Artist | Artist object (Zune-specific) |
| 0xB211 | MediaCard | Media card/collection |
| 0xB213 | Encounter | Social/community feature |
| 0xBA0B | AbstractMediacast | Podcast |
| 0xB802 | Firmware | Firmware update file |

## Vendor Properties (Zune-Specific)

### Track Properties (on MP3/WMA/AAC/M4A objects)

| Code | Name | Type | Access | Description |
|------|------|------|--------|-------------|
| 0xDA9A | Series Name | STRING | GET/SET | TV series name for video, used for grouping |
| 0xDA97 | Unknown | UINT128 | GET/SET | Unknown purpose |
| 0xDA99 | Unknown | UINT128 | GET/SET | Unknown purpose |
| 0xDA9B | Unknown | UINT8 | GET/SET | Enum: 0, 1 |
| 0xDA9E | Unknown | UINT32 | GET/SET | Unknown purpose |
| 0xDA00 | Unknown | UINT32 | GET/SET | Group 0x9 |
| 0xDA01 | Unknown | STRING | GET/SET | Group 0x9 |
| 0xDA02 | Unknown | UINT32 | GET/SET | Group 0x9 |
| 0xDA03 | Unknown | UINT16 | GET/SET | Enum: 0, 1 — Group 0x9 |
| 0xDA05 | Unknown | STRING | GET/SET | Group 0x9 |
| 0xDA82 | Unknown | UINT8 | GET/SET | Unknown purpose |
| 0xDAB0 | Unknown | UINT8 | GET/SET | Unknown purpose |
| 0xDAB2 | Unknown | UINT8 | GET/SET | Unknown purpose |
| 0xDAB4 | Unknown | UINT8 | GET/SET | Enum: 0, 1 |
| 0xD901 | Buy Flag | UINT8 | GET/SET | Enum: 0, 1 — Marketplace purchase indicator |

### Video Properties (on ASF/WMV objects, in addition to track properties)

| Code | Name | Type | Access | Description |
|------|------|------|--------|-------------|
| 0xDAB5 | Season Number | UINT32 | GET/SET | TV season number |
| 0xDAB6 | Episode Number | UINT32 | GET/SET | TV episode number |
| 0xDC87 | Width | UINT32 | GET/SET | Video width (range: 4-320 for WMV, 4-640 for ASF) |
| 0xDC88 | Height | UINT32 | GET/SET | Video height (range: 4-240 for WMV, 4-480 for ASF) |
| 0xDC95 | MetaGenre | UINT16 | GET/SET | Content classification |

### MetaGenre Values

| Value | Meaning | Zune UI Location |
|-------|---------|-----------------|
| 0x21 | Other | Videos |
| 0x23 | Music Video | Videos → Music Videos |
| 0x25 | Movie | Videos → Movies |
| 0x26 | TV Show | Videos → TV Shows |

### JPEG Properties

| Code | Name | Type | Range | Access |
|------|------|------|-------|--------|
| 0xDC87 | Width | UINT32 | 0-640 | GET/SET |
| 0xDC88 | Height | UINT32 | 0-480 | GET/SET |
| 0xDC4F | Non Consumable | UINT8 | 0, 1 | GET/SET |
| 0xDC47 | Date Authored | STRING | DATETIME | GET/SET |

### Album Properties (on AbstractAudioAlbum objects)

| Code | Name | Type | Access | Description |
|------|------|------|--------|-------------|
| 0xDC44 | Name | STRING | GET/SET | Album title |
| 0xDC46 | Artist | STRING | GET/SET | Album artist |
| 0xDC86 | Representative Sample Data | UINT8[] | GET/SET | Album art JPEG bytes |
| 0xDC81 | Representative Sample Format | UINT16 | GET/SET | Art format (14337 = JPEG) |
| 0xDC83 | Rep. Sample Height | UINT32 | READ ONLY | Art height (max 240) |
| 0xDC84 | Rep. Sample Width | UINT32 | READ ONLY | Art width (max 240) |
| 0xDAB9 | ArtistId | UINT32 | GET/SET | Handle of linked Artist object |

### Audio Codec Properties (READ ONLY — set by device from file analysis)

| Code | Name | Type | MP3 Values | WMA Values |
|------|------|------|-----------|-----------|
| 0xDE99 | AudioWAVECodec | UINT32 | 85 (0x0055) | 353, 354, 355 |
| 0xDE9A | AudioBitRate | UINT32 | 16000-323000 | 5000-1572864 |
| 0xDE93 | SampleRate | UINT32 | 8000-48000 | 8000-48000 |
| 0xDE94 | NumberOfChannels | UINT16 | 1, 2 | 1, 2 |

**Important:** These are READ ONLY on the Zune. The device reads them from the file header. Setting them via MTP has no effect on playback — but the initial values from `LIBMTP_new_track_t()` (all zeros) can confuse `adjust_u32()` during `LIBMTP_Update_Track_Metadata()`, potentially selecting wrong codec values. Set them explicitly before sending.

## Vendor Operations

### Known Operations

| Opcode | Name | Purpose |
|--------|------|---------|
| 0x9101 | GetSecureTimeChallenge | DRM clock |
| 0x9102 | GetSecureTimeResponse | DRM clock |
| 0x9108 | CleanDataStore | WMDRMPD — data store refresh |
| 0x9170 | OpenMediaSession | Media session management |
| 0x9171 | CloseMediaSession | Media session management |
| 0x9172 | GetNextDataBlock | Media session data |
| 0x9201 | ReportAddedDeletedItems | WMPPD — DB rebuild trigger |
| 0x9202 | ReportAcquiredItems | WMPPD — content acquisition |
| 0x9212 | SendWMDRMPDAppRequest | MTPZ auth (Steps 3, 5) |
| 0x9213 | GetWMDRMPDAppResponse | MTPZ auth (Step 4) |
| 0x9214 | EnableTrustedFileOperations | MTPZ auth (Step 6) |
| 0x9216 | EndTrustedAppSession | MTPZ auth reset (Step 2) |
| 0x1792 | ReadZMDB (vendor bulk) | Read Zune Media Database |

### Unknown Operations (from mtp-detect)

All fail on macOS via `LIBMTP_Custom_Operation` but are listed as supported:
```
0x9204, 0x9217, 0x9218, 0x9219, 0x921A, 0x921B, 0x921C, 0x921D
0x9220, 0x9221, 0x9222, 0x9223, 0x9224, 0x9225, 0x9226, 0x9227
0x9228, 0x9229, 0x922A, 0x922B, 0x922C, 0x922D, 0x922E, 0x922F
0x9230, 0x9231, 0x9232, 0x9240, 0x9242, 0x9243, 0x6108
```

### Finalization Operations

After syncing content to the device, these operations signal the Zune to rebuild its media database:

1. **0x9201** (ReportAddedDeletedItems) — fails on macOS but may trigger partial rebuild
2. **0x9108** (CleanDataStore) — succeeds on macOS, may trigger data refresh
3. **0x9202** (ReportAcquiredItems) — fails on macOS
4. **0x9171** (CloseMediaSession) — fails on macOS

**What actually triggers the rebuild:** Releasing the USB interface via `LIBMTP_Release_Device()` (which calls `ptp_closesession` + `close_usb`). The Zune detects the USB disconnection and automatically restarts + re-indexes. This is the same on both Linux and macOS — the vendor operations are decorative.

## Patched libmtp Functions

Standard libmtp maps vendor property codes (0xDA9A, 0xDAB5, 0xDAB6, etc.) to 0 through its `map_libmtp_property_to_ptp_property()` function. libzune's vendored libmtp adds three raw property setters that bypass this mapping:

```c
LIBMTP_Set_Object_String_Raw(device, object_id, ptp_property_code, string);
LIBMTP_Set_Object_u32_Raw(device, object_id, ptp_property_code, value);
LIBMTP_Set_Object_u16_Raw(device, object_id, ptp_property_code, value);
```

These are essential for setting TV series metadata (0xDA9A series name, 0xDAB5 season, 0xDAB6 episode) which standard libmtp cannot do.

## macOS-Specific Quirks

1. **libusb_detach_kernel_driver fails** — macOS claims USB devices via PTPCamera/AMPDeviceDiscoveryAgent. Must kill these daemons before connecting.

2. **All vendor operations (0x9201-0x9243) fail** — `LIBMTP_Custom_Operation` returns -1 for all vendor ops on macOS. The Zune still works because the USB disconnect triggers re-indexing.

3. **USB stall recovery needed** — macOS libusb bulk transfers can stall without returning timeout errors. `libusb_clear_halt()` on both endpoints + retry is required.

4. **ZMDB read works inside active PTP session** — despite being a raw bulk operation, the 0x1792 request works while a PTP session (sessionId=1) is active.

## ID3 Tag Compatibility

The Zune ONLY reads ID3v2.3 tags. ID3v2.4 tags are completely ignored, causing "Unknown Artist" / "Unknown Album" display. MP3 files must be retagged to ID3v2.3 before sending:

```
ffmpeg -i input.mp3 -c copy -map_metadata 0 -id3v2_version 3 -write_id3v1 1 -y output.mp3
```

## References

- mtp-detect output: `docs/mtp-detect-output.txt`
- PTP/MTP specification: USB-IF MTP 1.0/1.1
- zune-explorer `mtp-constants.js`: operation/property code definitions
- libmtp `music-players.h`: device flags
