# ZuneDB — Unified Device Library Engine

> **Documentation status (2026-10-04):** Historical design proposal, not a description of the complete implementation. Current API is zune_infiltrate, with an internal MTP playlist fallback. Classic sizes and some metadata remain unavailable. See API_REFERENCE.md and src/zmdb.c; do not treat the completeness claims or proposed names below as implemented contracts.

## Vision

ZuneDB replaces ZMDB. Not a wrapper around it — a complete replacement that reads the Zune's binary database and enriches it internally to deliver a 100% complete library with zero fallbacks.

**One function. Everything. No MTP enumeration. No fallbacks.**

```c
ZuneDBLibrary *lib = NULL;
zune_db_scan(dev, &lib);
// Done. Every track, video, photo, playlist, album, artist, genre.
// All metadata resolved. All references linked. Ready to display.
```

## Why Replace ZMDB?

The current ZMDB approach has gaps:
- Video MetaGenre is missing (we don't know which are Movies vs TV Shows)
- Classic Zunes don't include photos in the ZMDB descriptor map
- Playlists aren't fully parsed
- We fall back to slow MTP enumeration to fill gaps

ZuneDB eliminates these gaps by:
1. **Fully parsing the ZMDB binary** — including the 24 "unknown" bytes in video records
2. **Discovering descriptor mappings** instead of hardcoding them — scan all 96 descriptors
3. **Reading vendor properties where ZMDB genuinely lacks them** — but doing it internally, not as a fallback the caller manages

## Research Phase — What We Need to Figure Out

### 1. What's in video record bytes +8 through +31?

We parse 8 bytes of a 32-byte video record and call the rest "unknown." That's 24 bytes of data sitting right there. To decode them:

**Method:** Hex dump video records from a Zune with known content:
- A video we sent as a Movie (MetaGenre 0x25)
- A video we sent as a TV Show (MetaGenre 0x26) with series/season/episode
- A video we sent as a Music Video (MetaGenre 0x23)

Compare the "unknown" bytes across these records. If MetaGenre is at a fixed offset, we'll see 0x25/0x26/0x23 in the same position.

**Tool needed:** `zune_db_dump_raw()` — saves the raw ZMDB binary to a file for offline hex analysis.

### 2. Are there multiple ZMDB sections?

The 0x1792 request takes a 3-byte object ID. We only use `[0x03, 0x92, 0x1f]`. There may be:
- A "video library" section with full video metadata including MetaGenre
- A "photo library" section (would fix Classic devices)
- A "everything" section

**Experiment:** Try different object IDs and see what comes back.

### 3. Do Classic descriptors have photo data at different indices?

The HD descriptor map uses index 16 for photos. Classic doesn't map any index to photos. But there are 96 descriptors — we only check indices 1, 2, and 12 on Classic.

**Experiment:** Scan ALL 96 descriptors on a Classic device. Check entry counts and schema types for every descriptor that has data. Photos might be at index 16 on Classic too, just unmapped by zune-explorer.

### 4. What backwards varint fields exist on video records?

Track records have field 0x6c (disc number). Video records might have fields for:
- Series name
- Season/episode numbers
- Duration
- Dimensions

**Experiment:** Parse ALL backwards varint fields from every video record and log unknown field IDs with their data.

## Architecture

### `zunedb.c` — The Scanner

```c
int zune_db_scan(ZuneDeviceHandle dev, ZuneDBLibrary **out) {
    // Phase 1: Read raw ZMDB binary (0x1792)
    uint8_t *raw_data = NULL;
    size_t raw_len = 0;
    zmdb_read_raw(dev, &raw_data, &raw_len);

    // Phase 2: Parse headers, discover format version
    ZuneDBParser parser;
    zunedb_init_parser(&parser, raw_data, raw_len);

    // Phase 3: Discover descriptors — don't use hardcoded maps
    // Scan all 96, check which have entries, determine schema from atom_ids
    zunedb_discover_descriptors(&parser);

    // Phase 4: Build master index
    zunedb_build_index(&parser);

    // Phase 5: Parse ALL records
    zunedb_parse_tracks(&parser, lib);
    zunedb_parse_videos(&parser, lib);
    zunedb_parse_photos(&parser, lib);
    zunedb_parse_playlists(&parser, lib);
    zunedb_parse_albums(&parser, lib);
    zunedb_parse_artists(&parser, lib);
    zunedb_parse_genres(&parser, lib);

    // Phase 6: Enrichment — fill anything ZMDB genuinely doesn't have
    // Internal, NOT exposed as a fallback
    zunedb_enrich_videos(dev, lib);   // MetaGenre if not in binary
    zunedb_enrich_photos(dev, lib);   // MTP photo list if descriptors had none

    // Phase 7: Return
    free(raw_data);
    *out = lib;
    return 0;
}
```

### Data Structures

```c
typedef struct {
    uint32_t item_id;
    char *title;
    char *artist;
    char *album;
    char *album_artist;
    char *genre;
    char *filename;
    uint32_t duration_ms;
    uint64_t filesize;
    uint16_t track_number;
    uint16_t disc_number;
    uint16_t play_count;
    uint16_t codec_id;
    uint8_t  rating;
} ZuneDBTrack;

typedef struct {
    uint32_t item_id;
    char *title;
    char *filename;
    char *folder;
    char *series;
    uint32_t season;
    uint32_t episode;
    uint64_t filesize;
    uint32_t duration_ms;
    uint32_t codec_id;
    uint16_t metagenre;
    uint16_t width;
    uint16_t height;
} ZuneDBVideo;

typedef struct {
    uint32_t item_id;
    char *title;
    char *filename;
    char *photo_album;
    char *folder;
    uint64_t filesize;
    uint32_t parent_id;
} ZuneDBPhoto;

typedef struct {
    uint32_t playlist_id;
    char *name;
    uint32_t *track_ids;
    uint32_t track_count;
} ZuneDBPlaylist;

typedef struct {
    uint32_t album_id;
    char *title;
    char *artist;
    uint16_t year;
} ZuneDBAlbum;

typedef struct {
    uint32_t artist_id;
    char *name;
} ZuneDBArtist;

typedef struct {
    ZuneDBTrack    *tracks;     int track_count;
    ZuneDBVideo    *videos;     int video_count;
    ZuneDBPhoto    *photos;     int photo_count;
    ZuneDBPlaylist *playlists;  int playlist_count;
    ZuneDBAlbum    *albums;     int album_count;
    ZuneDBArtist   *artists;    int artist_count;

    int is_hd;
    int used_zmdb;
    uint32_t scan_ms;
} ZuneDBLibrary;
```

### API

```c
/* Complete device library scan */
int zune_db_scan(ZuneDeviceHandle dev, ZuneDBLibrary **out);
void zune_db_free(ZuneDBLibrary *lib);

/* Research/debug tools */
int zune_db_dump_raw(ZuneDeviceHandle dev, const char *output_path);
int zune_db_scan_descriptors(ZuneDeviceHandle dev);
```

## Implementation Plan

### Step 1: Research Tools
Build `zune_db_dump_raw()` and `zune_db_scan_descriptors()`. These let us analyze the binary data offline and discover what we're missing.

### Step 2: Hex Analysis
Dump ZMDB from a Zune with known videos (movies + TV shows). Compare video records byte-by-byte. Find MetaGenre and other fields in the "unknown" bytes.

### Step 3: Descriptor Discovery
Run `zune_db_scan_descriptors()` on both Classic and HD. Map every descriptor. Find photos on Classic.

### Step 4: Full Parser
Write `zunedb.c` — no "unknown" bytes, no hardcoded maps. Everything discovered dynamically.

### Step 5: Internal Enrichment
For metadata genuinely not in the binary (known after Steps 2-3), add targeted MTP reads INSIDE zunedb.c. Caller never sees MTP.

### Step 6: Swift Integration
Replace all separate calls with one `zune_db_scan()`.

## What This Replaced

**Before (7+ calls, caller managed fallbacks):**
```swift
zune_read_zmdb()          // partial — tracks, videos, photos only
zune_get_tracks()         // slow MTP fallback
zune_get_videos()         // slow MTP + per-object MetaGenre
zune_get_photos()         // slow MTP fallback for Classic
zune_get_playlists()      // always MTP
fetchDeviceAlbumArt()     // separate
fetchDeviceVideoThumbs()  // separate
fetchDevicePhotoThumbs()  // separate
```

**After (1 call — IMPLEMENTED):**
```swift
let library = try await mtpService.scanLibrary()
// Returns: tracks, videos (with MetaGenre), photos, playlists,
//          albums, artists, photo albums — all from one USB read
```

Art/thumbnail fetching stays separate (binary MTP data, progressive UI updates).

`zune_read_zmdb()` is now deprecated. Use `zune_db_scan()`.

## What Makes This Unique

- **No fallbacks** — complete data or error
- **Discovery-based** — finds descriptors, doesn't hardcode them
- **Research-driven** — analyzed actual bytes before assuming
- **Complete video metadata** — MetaGenre discovered at ZMDB offset +38
- **Both Classic and HD** — one code path, one discovery algorithm
- **Pure C** — zero overhead, any language can call it

Nobody else has built this. We did.
