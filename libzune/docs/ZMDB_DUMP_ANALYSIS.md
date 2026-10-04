# ZMDB Dump Analysis — Zuuned Zune (Classic, ZMed v2)

> **Documentation status (2026-10-04):** Historical analysis of the specific captured database below. Its counts, names and offsets are evidence from that sample, not current device state or universal model guarantees.

## Device Info
- **Name:** Zuuned
- **Model:** Zune (Classic)
- **ZMed Version:** 2
- **ZMDB Size:** 339,364 bytes (331.4 KB)
- **Index Entries:** 2,348
- **Unique Strings:** 1,942

## Content Summary
| Type | Count |
|------|-------|
| Music Tracks | 1,599 |
| Videos | 8 |
| Photos | 13 |
| Playlists | 1 |
| Albums | 212 |
| Artists | 178 |
| Genres | 36 |
| Photo Albums | 4 |
| Video Titles | 4 |
| Filenames | 286 |

## Genres Found
- Alternative
- Blues
- Classical
- Industrial
- Metal
- Punk
- Rock
- Soundtrack

## Videos
- Disenchantment - S01E01
- Disenchantment - S01E02
- Hackers.1995.REMASTERED.REPACK.1080p.BluRay.x264.AAC5.1-[YTS.MX].wmv
- Mister Roberts 1955.wmv
- SAO Abridged - S01E01
- SAO Abridged - S01E01 - SAO Abridged Parody Episode 01.mp4
- SAO Abridged - S01E02
- SAO Abridged - S01E03
- SAO Abridged - S01E04

## Video Record Hex Analysis

### Record 1: Hackers (Movie)
```
0000: 3c 01 00 05 fb 07 00 0a 00 00 00 00 00 00 00 00
0010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0020: 81 b9 00 00 00 00 02 00 48 61 63 6b 65 72 73...
                              ^^^^ ^^^^
                              WMV   Movie (0x02)
```
- **+0:** folderRef = 0x0500013c
- **+4:** titleRef = 0x0a0007fb
- **+32:** formatCode = 0xB981 (WMV)
- **+36:** zuneType = 0x0002 → **Movie**
- **+38:** title = "Hackers.1995.REMASTERED..."

### Record 2: SAO Abridged S01E01 (TV Show)
```
0000: ff ff ff 05 c4 07 00 0a 00 00 00 00 00 00 00 00
0010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0020: 82 b9 00 00 00 00 04 00 53 41 4f 20 41 62 72 69...
                              ^^^^ ^^^^
                              MP4   TV Show (0x04)
```
- **+32:** formatCode = 0xB982 (MP4)
- **+36:** zuneType = 0x0004 → **TV Show**
- **+38:** title = "SAO Abridged - S01E01"

### Discovered MetaGenre Mapping
| ZMDB +36 Value | MTP MetaGenre | Zune UI |
|----------------|---------------|---------|
| 0x0002 | 0x25 | Movie |
| 0x0004 | 0x26 | TV Show |
| 0x0001 | 0x23? | Music Video (unconfirmed) |
| other | 0x21 | Other |

## Descriptor Map (All 96 scanned)

| Desc | EntrySize | Count | Schema |
|------|-----------|-------|--------|
| 0 | 8 | 2,348 | Index Table |
| 1 | 4 | 1,599 | Music |
| 2 | 4 | 933 | Music (sorted?) |
| 3 | 4 | 212 | Album |
| 4 | 4 | 178 | Artist |
| 5 | 4 | 36 | Genre |
| 6 | 4 | 1,599 | Music (by album?) |
| 7 | 4 | 212 | Album (sorted?) |
| 8 | 4 | 1,599 | Music (by artist?) |
| 10 | 4 | 1,599 | Music (by genre?) |
| 11 | 4 | 1 | Playlist |
| 12 | 4 | 8 | Video |
| 13 | 4 | 286 | Filename |
| 15 | 4 | 4 | PhotoAlbum |
| 16 | 4 | 13 | Picture |
| 17 | 4 | 13 | Picture (duplicate?) |
| 21 | 4 | 4 | VideoTitle |
| 25 | 4 | 5 | Unknown (0x17) |

**Note:** Descriptors 33+ appear to be secondary indices, sort orders, or extended metadata. Most have very large entry sizes (256-5888) suggesting they contain embedded data rather than simple atom_id references.
