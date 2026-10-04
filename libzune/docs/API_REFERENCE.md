# libzune API Reference

C library for Zune device management over USB. Native PTP/MTP/MTPZ stack.
Supports macOS (DriverKit) and Linux (libusb).

**79 public functions.** All prefixed `zune_`. Thread-local error via `zune_get_error()`.

## Quick Start

```c
#include <zune.h>

int main(void) {
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) { fprintf(stderr, "No Zune: %s\n", zune_get_error()); return 1; }

    printf("Connected: %s (%s)\n", zune_get_name(dev), zune_get_model(dev));
    printf("Battery: %d%%, Free: %llu MB\n",
           zune_get_battery(dev), zune_get_headroom(dev) / (1024*1024));

    // Scan entire library in one USB call
    ZuneDBLibrary *lib = NULL;
    if (zune_infiltrate(dev, &lib) == 0) {
        printf("%d tracks, %d videos, %d photos\n",
               lib->track_count, lib->video_count, lib->photo_count);

        // Check for duplicate before uploading
        if (zune_find_track(dev, "My Song", "My Artist", "My Album") == 0) {
            uint32_t id = 0;
            zune_smuggle_track(dev, "song.mp3", &id);
            zune_verify(dev, id, "My Song", "My Artist", 0);
        }

        zune_free_scan(lib);
    }

    zune_finalize(dev);
    zune_sever(dev);
    return 0;
}
```

Build: `cc -I/path/to/libzune/include app.c -L/path/to/lib -lzune -lgcrypt -lgpg-error`

---

## 1. Connection Lifecycle

| Function | Returns | Description |
|----------|---------|-------------|
| `zune_breach()` | `ZuneDeviceHandle` or NULL | Connect + MTPZ auth. Blocks until ready. |
| `zune_sever(dev)` | void | Disconnect and free. Safe during transfers. |
| `zune_is_live(dev)` | 1 or 0 | Check if device is still connected. |
| `zune_rename(dev, name)` | 0 or -1 | Set device friendly name. |
| `zune_finalize(dev)` | 0 or -1 | Signal device to rebuild its media database. Call after sync. |

```c
ZuneDeviceHandle zune_breach(void);
void zune_sever(ZuneDeviceHandle dev);
int  zune_is_live(ZuneDeviceHandle dev);
int  zune_rename(ZuneDeviceHandle dev, const char *new_name);
int  zune_finalize(ZuneDeviceHandle dev);
```

## 2. Device Information

| Function | Returns | Description |
|----------|---------|-------------|
| `zune_get_name(dev)` | `const char *` | Friendly name (e.g. "badger's Zune") |
| `zune_get_model(dev)` | `const char *` | Model string from USB |
| `zune_get_serial(dev)` | `const char *` | Serial number |
| `zune_get_battery(dev)` | `uint8_t` | Battery 0-100% |
| `zune_get_capacity(dev)` | `uint64_t` | Total storage bytes |
| `zune_get_headroom(dev)` | `uint64_t` | Free storage bytes |
| `zune_identify(dev)` | `ZuneModel` | Model for transcode profiles |
| `zune_get_family(dev)` | `ZuneDeviceFamily` | Hardware generation (Keel/Scorpius/Draco/Pavo) |
| `zune_is_hdd(dev)` | 1 or 0 | HDD-based device (Keel=Zune30, Draco=Zune80/120) |

### Enums

```c
typedef enum {
    ZUNE_MODEL_30, ZUNE_MODEL_80, ZUNE_MODEL_HD, ZUNE_MODEL_HD_720P, ZUNE_MODEL_UNKNOWN
} ZuneModel;

typedef enum {
    ZUNE_FAMILY_KEEL     = 0x00,  // Zune 30 (HDD)
    ZUNE_FAMILY_SCORPIUS = 0x02,  // Zune 4/8/16 (flash)
    ZUNE_FAMILY_DRACO    = 0x03,  // Zune 80/120 (HDD)
    ZUNE_FAMILY_PAVO     = 0x06,  // Zune HD
    ZUNE_FAMILY_UNKNOWN  = 0xFF
} ZuneDeviceFamily;
```

## 3. Library Scanning (ZMDB)

Single USB call returns the entire device library. 10-100x faster than MTP enumeration.

```c
int  zune_infiltrate(ZuneDeviceHandle dev, ZuneDBLibrary **out);
void zune_free_scan(ZuneDBLibrary *lib);
```

### ZuneDBLibrary

```c
typedef struct {
    ZuneTrack        *tracks;        int track_count;
    ZuneVideoFile    *videos;        int video_count;
    ZunePhotoFile    *photos;        int photo_count;
    ZunePlaylist     *playlists;     int playlist_count;
    ZuneDBAlbum      *albums;        int album_count;
    ZuneDBArtist     *artists;       int artist_count;
    ZuneDBPhotoAlbum *photo_albums;  int photo_album_count;
    int is_hd;
    uint32_t scan_ms;
} ZuneDBLibrary;
```

Also caches the library in the device handle for duplicate detection (`zune_find_*`).

### Research Tools

```c
int zune_dump_raw(ZuneDeviceHandle dev, const char *output_path);   // raw ZMDB binary
int zune_infiltrate_deep(ZuneDeviceHandle dev);                     // scan all 96 descriptors
```

## 4. Music Operations

### ZuneTrack

```c
typedef struct {
    uint32_t item_id;
    char    *title, *artist, *album, *genre;
    uint32_t duration_ms;
    uint64_t filesize;
    uint16_t tracknumber;
    uint16_t disc_number;     // ZMDB varint 0x6C
    uint16_t playcount;       // ZMDB offset +26 (HD)
    uint8_t  rating;          // 0=neutral, 8=liked, 3=disliked
    uint32_t skip_count;      // ZMDB varint 0x63
    uint64_t last_played;     // ZMDB varint 0x70 (Windows FILETIME)
} ZuneTrack;
```

### Enumeration

```c
ZuneTrack *zune_get_tracks(ZuneDeviceHandle dev, int *count);  // MTP fallback
void       zune_free_tracks(ZuneTrack *tracks, int count);
```

### Upload (Smuggle)

```c
int zune_smuggle_track(dev, filepath, *out_item_id);
int zune_smuggle_track_tagged(dev, filepath, title, artist, album, genre,
                               tracknumber, duration_ms, *out_item_id);
int zune_smuggle_track_ex(dev, filepath, title, artist, album, genre,
                           tracknumber, duration_ms, progress_fn, userdata, *out_item_id);
```

### Delete / Download

```c
int zune_purge_track(dev, item_id);
int zune_extract_track(dev, item_id, dest_path);
```

### Track State (Playcount / Rating)

```c
int zune_get_track_state(dev, item_id, *playcount, *rating, *skip_count);
int zune_set_track_state(dev, item_id, playcount, rating);
```

Rating encoding: `0` = neutral, `8` = liked (heart), `3` = disliked (broken heart).

### Verification

```c
int zune_verify(dev, item_id, expected_title, expected_artist, expected_size);
```

Reads back properties after upload and compares. Returns 0 if all match.

## 5. Video Operations

### ZuneVideoFile

```c
typedef struct {
    uint32_t item_id;
    char    *filename;
    uint64_t filesize;
    uint16_t metagenre;    // ZUNE_METAGENRE_MOVIE / TV_SHOW / MUSIC_VIDEO / OTHER
    uint32_t parent_id;
} ZuneVideoFile;
```

### Upload

```c
int zune_smuggle_movie(dev, filepath, display_name, description,
                        poster_jpeg, poster_len, *out_item_id);
int zune_smuggle_episode(dev, filepath, display_name, series, season, episode,
                          description, poster_jpeg, poster_len, *out_item_id);
int zune_smuggle_clip(dev, filepath, display_name,
                       poster_jpeg, poster_len, *out_item_id);
int zune_smuggle_other(dev, filepath, display_name, description,
                        poster_jpeg, poster_len, *out_item_id);
```

### Series Info / Delete / Download

```c
int zune_get_series_info(dev, item_id, **series, *season, *episode, **title);
int zune_purge_video(dev, item_id);
int zune_extract_video(dev, item_id, dest_path);
```

## 6. Photo Operations

### Upload / Delete / Download

```c
char *zune_arm_photo(input_path);                          // resize for Zune
int   zune_smuggle_photo(dev, filepath, album_name);       // upload (auto-resizes)
int   zune_purge_photo(dev, item_id);
int   zune_extract_photo(dev, item_id, dest_path);
int   zune_get_dimensions(dev, item_id, *width, *height);  // MTP 0xDC87/0xDC88
```

### Photo Albums

```c
ZunePhotoAlbum *zune_get_photo_albums(dev, photos, photo_count, *album_count);
void            zune_free_photo_albums(albums, count);
```

## 7. Playlists

```c
ZunePlaylist *zune_get_playlists(dev, *count);
void          zune_free_playlists(playlists, count);
uint32_t      zune_forge_playlist(dev, name, track_ids, count);    // create
int           zune_rewire_playlist(dev, playlist_id, name, track_ids, count);  // update
int           zune_purge_playlist(dev, playlist_id);               // delete
```

## 8. Albums & Artists

The Zune reads album/artist metadata from MTP abstract objects, not track properties.

```c
uint32_t zune_forge_album(dev, album_name, artist_name, genre, track_ids, track_count);
int      zune_rewire_album(dev, album_id, album_name, artist_name, track_ids, track_count);
uint32_t zune_forge_artist(dev, artist_name);
int      zune_link_artist(dev, object_id, artist_handle);
int      zune_brand(dev, item_id, jpeg_data, jpeg_len);      // set album art

ZuneAlbumObject *zune_get_albums(dev, *count);
void             zune_free_albums(albums, count);
```

## 9. Duplicate Detection

Search the cached ZMDB library (zero MTP calls). Requires prior `zune_infiltrate()`.

```c
uint32_t zune_find_track(dev, title, artist, album);  // returns item_id or 0
uint32_t zune_find_video(dev, filename);
uint32_t zune_find_photo(dev, filename);
```

**Example: skip duplicates during sync**
```c
ZuneDBLibrary *lib = NULL;
zune_infiltrate(dev, &lib);

for (int i = 0; i < queue_count; i++) {
    if (zune_find_track(dev, queue[i].title, queue[i].artist, queue[i].album)) {
        printf("skipping duplicate: %s\n", queue[i].title);
        continue;
    }
    zune_smuggle_track(dev, queue[i].filepath, NULL);
}

zune_free_scan(lib);
```

## 10. Thumbnails & Art

```c
int zune_brand(dev, item_id, jpeg_data, jpeg_len);        // set art (representative sample)
int zune_grab_thumb(dev, item_id, cache_path);             // get art (rep sample only)
int zune_grab_photo_thumb(dev, item_id, cache_path);       // get photo thumb (with fallback)
```

## 11. Transcoding

```c
char *zune_arm_audio(input_path);                                    // → MP3 320kbps
char *zune_retag(mp3_path);                                          // ID3v2.4 → v2.3
char *zune_arm_video(input_path, model);                             // → Zune-compatible
char *zune_arm_episode(input_path, model, series, season, episode);  // → video + metadata
char *zune_arm_photo(input_path);                                    // → 480px JPEG
```

All return temp file path (caller frees string + unlinks file). NULL on failure.

## 12. Control & Recovery

```c
void zune_abort(dev);          // request cancellation (thread-safe)
int  zune_is_aborted(dev);     // check if cancelled
void zune_clear_abort(dev);    // reset before new transfer
int  zune_unjam(dev);          // clear USB stall on both endpoints
```

## 13. Folders

```c
uint32_t zune_forge_folder(dev, name, parent_id);                    // create
int      zune_get_folders(dev, **entries);                            // list
void     zune_free_folders(entries, count);
```

## 14. Metadata Extraction

```c
int  zune_probe(filepath, *metadata);      // read tags via ffprobe
void zune_free_metadata(*metadata);
int  zune_decode_filename(filepath, **series, *season, *episode);  // parse "S01E05"
int  zune_snap_thumb(video_path, output_path);                     // ffmpeg frame grab
```

## 15. Error Handling

```c
const char *zune_get_error(void);  // thread-local, set by failed operations
```

All functions that return `int` use: `0` = success, `-1` = failure.
Functions returning pointers use `NULL` = failure.
Error details available via `zune_get_error()` after any failure.

---

## Data Types Reference

| Type | Fields | Source |
|------|--------|--------|
| `ZuneTrack` | item_id, title, artist, album, genre, duration_ms, filesize, tracknumber, disc_number, playcount, rating, skip_count, last_played | ZMDB |
| `ZuneVideoFile` | item_id, filename, filesize, metagenre, parent_id | ZMDB |
| `ZunePhotoFile` | item_id, filename, filesize, parent_id | ZMDB |
| `ZunePlaylist` | playlist_id, name, track_ids, track_count | MTP |
| `ZuneDBAlbum` | album_id, title, artist, year | ZMDB |
| `ZuneDBArtist` | artist_id, name | ZMDB |
| `ZuneDBPhotoAlbum` | album_id, name | ZMDB |
| `ZuneAlbumObject` | album_id, name, artist, track_ids, track_count | MTP |
| `ZuneMetadata` | title, artist, album, genre, tracknumber, duration_ms | ffprobe |
| `ZuneFolderEntry` | id, name | MTP |
| `ZuneDBLibrary` | all of the above + is_hd, scan_ms | ZMDB |
