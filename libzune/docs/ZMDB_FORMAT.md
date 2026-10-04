# ZMDB Binary Format — Zune Media Database

## Overview

The Zune stores its entire media library in a proprietary binary database called ZMDB (Zune Media DataBase). This database can be read via vendor MTP opcode `0x1792` — a raw USB bulk pipe operation that returns the complete database in one transfer.

This document describes the binary format based on reverse engineering from:
- zune-explorer's `zmdb-parser.js` (NiceBeard)
- XuneSyncLibrary by magicisinthehole (original reverse engineering)
- Our own analysis of raw ZMDB dumps from Zune 80/120 and Zune HD devices

## Reading the ZMDB

### USB Protocol

The ZMDB read is NOT a standard MTP/PTP operation. It uses a raw bulk pipe:

**Request (16 bytes):**
```
Offset  Value   Description
0x00    0x10    Length (always 16)
0x01    0x00    Padding
0x02    0x00    Padding
0x03    0x00    Padding
0x04    0x01    Command marker
0x05    0x00    Padding
0x06    0x17    Opcode high byte
0x07    0x92    Opcode low byte (combined: 0x1792)
0x08    0x03    Object ID byte 0 (music library default)
0x09    0x92    Object ID byte 1
0x0A    0x1f    Object ID byte 2
0x0B    0x00    Padding
0x0C    0x01    Trailer
0x0D    0x00    Padding
0x0E    0x00    Padding
0x0F    0x00    Padding
```

**Important:** This request is sent on the SAME bulk endpoints used for PTP traffic, and it works INSIDE an active PTP session. The Zune handles the non-PTP packet and responds with the database blob.

**Timing:** Wait 250ms between sending the request and reading the response.

**Response:**
```
Offset  Type      Description
0x00    uint32LE  Total response size (header + payload)
0x04    8 bytes   Header metadata (unknown purpose)
0x0C    ...       Payload (totalSize - 12 bytes)
```

Read in 64KB chunks. After payload is complete, drain residual data with a 512-byte read (100ms timeout, errors expected and ignored).

### Object ID

The 3-byte object ID at request offset 0x08-0x0A selects which database section to read. Known values:
- `[0x03, 0x92, 0x1f]` — music library (default, returns tracks + videos + photos on HD)

**Research needed:** Other object IDs may select video-only, photo-only, or other sections.

## Binary Structure

### Header

```
Offset  Size  Description
0x00    4     Magic: "ZMDB" (0x5A, 0x4D, 0x44, 0x42)
0x20    4     ZMed header: "ZMed" (0x5A, 0x4D, 0x65, 0x64)
0x24    2     ZMed version (uint16LE)
              2 = Classic (Zune 30/80/120, flash 4/8/16)
              5 = Zune HD
```

### ZArr Descriptor Block

Search bytes 0x30 through 0x100 for the 4-byte marker "ZArr". This marks the start of the descriptor table.

**96 descriptors, 20 bytes each:**
```
Offset  Type      Description
+0      6 bytes   Flags/header (unknown)
+6      uint16LE  Entry size (fixed portion of each record)
+8      uint32LE  Entry count (number of records)
+16     uint32LE  Data offset (absolute offset to first record)
```

### Descriptor 0: Index Table

Special — contains the master index mapping atom IDs to record offsets.

**Entry format (8 bytes each):**
```
Offset  Type      Description
+0      uint32LE  Atom ID (schema type in upper byte)
+4      uint32LE  Record offset (absolute position in ZMDB)
```

### Descriptor-to-Schema Mapping

**Zune HD:**
| Descriptor | Schema | Type |
|-----------|--------|------|
| 1 | 0x01 | Music (tracks) |
| 11 | 0x07 | Playlist |
| 12 | 0x02 | Video |
| 16 | 0x03 | Picture |
| 19 | 0x10 | Podcast Episode |
| 26 | 0x12 | Audiobook Track |

**Classic:**
| Descriptor | Schema | Type |
|-----------|--------|------|
| 1 | 0x01 | Music (tracks) |
| 11 | 0x07 | Playlist |
| 12 | 0x02 | Video |
| 16 | 0x03 | Picture |

Note: zune-explorer's original Classic map omitted descriptors 11 (playlists) and 16 (pictures). Our descriptor scan confirmed both are present on Classic devices (tested on Zune 80 and Zune 120). Photos are at the same descriptor index (16) on both Classic and HD.

## Schema Types

Upper byte of each atom_id identifies the schema:

| Byte | Type | Description |
|------|------|-------------|
| 0x01 | Music | Audio tracks |
| 0x02 | Video | Video files |
| 0x03 | Picture | Photo files |
| 0x05 | Filename | Referenced filename strings |
| 0x06 | Album | Album metadata |
| 0x07 | Playlist | Playlist with track refs |
| 0x08 | Artist | Artist metadata |
| 0x09 | Genre | Genre name |
| 0x0A | VideoTitle | Video title reference |
| 0x0B | PhotoAlbum | Photo album/collection |
| 0x0C | Collection | Generic collection |
| 0x0F | PodcastShow | Podcast series |
| 0x10 | PodcastEpisode | Individual episode |
| 0x11 | AudiobookTitle | Audiobook title |
| 0x12 | AudiobookTrack | Audiobook track |

## Record Format

### Record Header

Each record is preceded by a 4-byte header at `offset - 4`:
```
uint32LE: (size & 0x00FFFFFF) | (flags << 24)
```
- Lower 24 bits: record size in bytes
- Upper 8 bits: flags (bit 31 must be 0 for valid records)

### Music Record (Schema 0x01)

**Zune HD (32 bytes fixed):**
```
Offset  Type      Field
+0      uint32LE  albumRef (atom_id of Album record)
+4      uint32LE  artistRef (atom_id of Artist record)
+8      uint32LE  genreRef (atom_id of Genre record)
+12     uint32LE  filenameRef (atom_id of Filename record)
+16     int32LE   duration (milliseconds)
+20     int32LE   filesize (bytes)
+24     uint16LE  trackNumber
+26     uint16LE  playCount
+28     uint16LE  codecId
+30     uint8     rating (0-100)
+32     ...       title (UTF-8, null-terminated)
```

**Classic (28 bytes fixed):**
Same as HD but without codecId (offset 28) and rating (offset 30). Title starts at offset 28.

### Video Record (Schema 0x02)

**40 bytes fixed portion + variable title (confirmed via hex dump diagnostic 2026-04-15):**
```
Offset  Type      Field
+0      uint32LE  folderRef (parent folder atom_id, always 0x05FFFFFF)
+4      uint32LE  titleRef (VideoTitle atom_id, schema 0x0A — resolves EMPTY, do not use)
+8      uint32LE  unknown (always 0)
+12     uint32LE  filenameRef (always 0x00000000 — videos don't use filename refs)
+16     uint32LE  filesize (bytes, sometimes 0)
+20     12 bytes  zeros (reserved/unknown)
+32     uint16LE  formatCode — 0xB981 = WMV, 0xB982 = MP4/H.264
+34     uint16LE  padding (always 0)
+36     uint16LE  padding (always 0)
+38     uint16LE  zuneType — VIDEO CATEGORY (MetaGenre equivalent)
+40     ...       title (UTF-8, null-terminated) — THIS IS THE DISPLAY NAME
```

#### How the video title was found (2026-04-15)

**Problem:** Every video on both Zune 30 and Zune HD had an empty filename
in the ZMDB scan. The parser was resolving `filenameRef` at offset +12 via
`zmdb_resolve_string()`, which walks the index table to find the referenced
record and extracts its string. But `filenameRef` was always `0x00000000` —
there's no filename reference for video records. The `titleRef` at +4 had
valid-looking atom IDs (like `0x0A00020A`, schema 0x0A = VideoTitle) but
the resolver also returned empty because schema 0x0A isn't handled by
`zmdb_resolve_string()` — that function only knows how to extract strings
from Filename (0x05), Genre (0x09), Artist (0x08), and Album (0x06) schemas.

**Investigation:** A hex dump diagnostic was added to `zmdb_parse_video()`
that printed all 4 reference fields, the fixed fields at +16 through +38,
the raw bytes at offset +40, and the resolved strings for both `titleRef`
and `filenameRef`. Output from a Zune 30 with known content:

```
[zmdb-video] atom=02000208 rec_size=568 +38=0x0002
  refs: folder=05FFFFFF title=0A00020A +8=00000000 filename=00000000
  +40 title: "Blade Runner.wmv"
  titleRef resolved: ""
  filename resolved: ""
```

The title was sitting at offset +40 in plain UTF-8 the entire time. Every
video record followed the same pattern — the display name (set via MTP
Name property 0xDC44 during `zune_smuggle_movie/episode/clip`) is stored
as a null-terminated UTF-8 string immediately after the 40-byte fixed
portion.

**Why this works the same as music tracks:** Music records store their
title at offset `ENTRY_SIZE_MUSIC_HD` (32) or `ENTRY_SIZE_MUSIC_CLASSIC`
(28) — right after their fixed fields. Video records store their title
at offset 40 — right after their fixed fields (`ENTRY_SIZE_VIDEO` = 32,
but the actual fixed portion extends to 40 bytes including formatCode
and zuneType). The ZMDB uses a consistent pattern: fixed fields first,
then variable-length title string.

**What the title contains:** The MTP Name property value set during upload.
For movies: `"Blade Runner.wmv"` (display name + extension from sync).
For TV episodes: `"SAO Abridged - S01E01"` (series - SxxExx format).
For Vox Machina: `"The.Legend.of.Vox.Machina. - S01E01"` (dots from
original filename still present — the title cleaner hadn't been applied
at sync time for this batch).

**Confirmed on both device families:**
- Zune 30 (Keel, 0xD21A = 0x00000001): 38 videos, all titles at +40
- Zune HD (Pavo, 0xD21A = 0x06040001): 23 videos, all titles at +40
- Record sizes vary (568-1191 bytes) — the variable portion after the
  title contains backwards varint fields (same as music tracks)

#### zuneType → MTP MetaGenre Mapping (offset +38)

Discovered April 2026 via hex dump analysis of Zune 80 with known movies and TV shows.

| zuneType (ZMDB +38) | MTP MetaGenre | Zune UI Category |
|---------------------|---------------|------------------|
| 0x0002 | 0x25 | Movie |
| 0x0004 | 0x26 | TV Show |
| 0x0001 | 0x23 | Music Video (unconfirmed, needs sample) |
| other | 0x21 | Other |

**Evidence:**
- "Hackers.1995" (synced as Movie): `+38 = 02 00` → 0x0002 → Movie ✓
- "Mister Roberts 1955" (synced as Movie): `+38 = 02 00` → 0x0002 → Movie ✓
- "SAO Abridged - S01E01" (synced as TV Show): `+38 = 04 00` → 0x0004 → TV Show ✓
- 5 additional TV episodes all show `+38 = 04 00` ✓

This eliminates the need for per-video MTP property reads to determine MetaGenre. The ZMDB binary contains the video category directly.

#### formatCode Values (offset +32)

| formatCode | Format |
|-----------|--------|
| 0xB981 | WMV (Windows Media Video) |
| 0xB982 | MP4 / H.264 |

### Picture Record (Schema 0x03)

**24 bytes fixed:**
```
Offset  Type      Field
+0      uint32LE  folderRef (parent folder)
+4      uint32LE  albumRef (PhotoAlbum atom_id)
+8      uint32LE  collectionRef
+12     uint32LE  filenameRef
+16     8 bytes   UNKNOWN
+24     ...       title (UTF-8, null-terminated)
```

### Album Record (Schema 0x06)

**Zune HD (20 bytes fixed):**
```
Offset  Type      Field
+0      uint32LE  artistRef
+4      8 bytes   Reserved
+12     uint64LE  FILETIME (Windows timestamp → release year)
+20     ...       title (UTF-8, null-terminated)
```

**Classic (12 bytes fixed):**
```
Offset  Type      Field
+0      uint32LE  artistRef
+4      8 bytes   Reserved
+12     ...       title (UTF-8, null-terminated)
```

### Artist Record (Schema 0x08)

**Zune HD (4 bytes fixed):**
```
Offset  Type      Field
+0      4 bytes   Reserved
+4      ...       name (UTF-8, null-terminated)
```

**Classic (1 byte fixed):**
```
Offset  Type      Field
+0      uint8     flags
+1      ...       name (UTF-8, null-terminated)
```

### Genre Record (Schema 0x09)

```
Offset  Type      Field
+0      uint8     flags
+1      ...       name (UTF-8, null-terminated)
```

### Filename Record (Schema 0x05)

```
Offset  Type      Field
+0      8 bytes   Reserved
+8      ...       filename (UTF-8, null-terminated)
```

## Backwards Varint Fields

Records can have variable-length fields appended AFTER the null-terminated string. These are read BACKWARDS from the end of the record.

### Format (per field, from end):
1. **Field ID** (1-2 bytes): if high bit set in first byte, read second byte
   - Combined: `(byte2 << 7) | (byte1 & 0x7F)`
2. **Field Size** (1-3 bytes): if high bit set, multi-byte
3. **Field Data** (fieldSize bytes)

### Known Field IDs:
| ID | Content | Found In |
|----|---------|----------|
| 0x44 | UTF-16LE filename | Album, Artist, Video |
| 0x6C | Disc number (uint32) | Music |
| 0x14 | GUID (16 bytes) | Artist |

## Reference Resolution

To resolve a reference (e.g., albumRef in a Music record):
1. Look up the atom_id in the index table (descriptor 0)
2. Read the record at the returned offset
3. Parse the record according to its schema type
4. Extract the string (title, name, etc.)

## FILETIME Conversion

Album records on Zune HD contain a Windows FILETIME at offset +12:
```
ticks = uint64LE
seconds = ticks / 10,000,000
unix_timestamp = seconds - 11,644,473,600
year = gmtime(unix_timestamp).tm_year + 1900
```

## Resolved Questions

1. **Video MetaGenre location** — Found at offset +38 (zuneType field). Maps: 0x0002=Movie, 0x0004=TV Show. Confirmed on Zune 80 Classic with 8 videos.
2. **Classic photos in ZMDB** — YES, descriptor 16 has photos on Classic. zune-explorer's Classic map simply omitted it. Tested on two Classic devices (101 and 13 photos respectively).
3. **Classic playlists in ZMDB** — Descriptor 11 has playlists on Classic (found via descriptor scan).
4. **Video formatCode** — At offset +32: 0xB981=WMV, 0xB982=MP4.

## Open Questions

1. What data is in video record bytes +8 through +11? (always 0 in samples)
2. Is +16 filesize or duration? Need cross-reference with MTP properties.
3. What's in the 12 zero bytes at +20 through +31?
4. Are there additional backwards varint field IDs we haven't discovered?
5. Can different object IDs in the 0x1792 request select different database sections?
6. Do podcast and audiobook records follow similar patterns to music/video?
7. What is zuneType 0x0001? (guessed as Music Video, needs a device with music videos)

## Tools

- `mtp-detect` — dumps device capabilities and supported properties
- `zune_db_dump_raw()` — saves raw ZMDB binary to file for offline hex analysis
- `zune_db_scan_descriptors()` — scans and prints all 96 descriptors with entry counts and schema types
- Hex editor — for analyzing unknown record fields
