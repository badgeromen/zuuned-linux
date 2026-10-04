# libzune API reference

Current public interface: **90 functions** in [include/zune.h](../include/zune.h).
This reference describes the implementation as reviewed on October 4, 2026.
The exact declarations below are generated; [TOC.md](TOC.md) links each one to
its implementation. Build and platform requirements are in [README](../README.md).

## Connection, threads and ownership

Calls block. Serialize operations for a device and dispatch them from a worker,
not a GUI thread. The library does not own your worker thread. `zune_abort`
requests cooperative cancellation; wait for the active call to return before
clearing cancellation or tearing down. **Do not call `zune_sever` while another
operation uses the handle.** Its short delay is not a worker join. Abrupt termination can leave an incomplete device object.

`zune_breach` opens a session and attempts authentication when data is available.
It can return a handle even when authentication fails or no data is available.
It does not scan the library. `zune_is_live` checks local handle/backend flags,
not USB responsiveness. See [BUILD_WITH_MTPZ.md](BUILD_WITH_MTPZ.md).

Device name/model/serial pointers are borrowed and valid until disconnect.
Enumeration results belong to the caller and use their matching free function.
Do not separately free records within a `ZuneDBLibrary`; `zune_free_scan` owns
all of them. Plain allocated outputs (object names, references, series/title
strings) use `free`. Prepared-media paths require both `unlink` and `free`.

**Scan cache lifetime:** a successful `zune_infiltrate` stores a borrowed pointer
to the returned scan in the device. Keep that scan alive for all `zune_find_*`
calls. `zune_free_scan` does not clear the device pointer. After freeing it, do
not call a find helper until another successful scan replaces the cache. A
failed scan does not replace it. The simplest lifetime is scan, search/use,
sever, free scan. Scans are snapshots, not a live index of subsequent uploads.

## Return values and diagnostics

Most mutation functions return 0 for success and -1 for failure, but this is
**not a universal rule**:

- Forge functions return a nonzero object ID, or 0 on failure.
- `zune_get_folders` returns a count or -1. Other enumeration functions return
  an allocated array and count; NULL with zero count can mean empty or failure.
  Some enumeration paths skip unreadable records, so results may be partial.
- Boolean helpers and `zune_decode_filename` return 1/0.
- `zune_smuggle_video_named` returns 1 (`ZUNE_VIDEO_METADATA_INCOMPLETE`) when
  media is uploaded but requested metadata/art was not fully saved. Its returned
  object ID is valid: repair that object, **do not upload it again**.
- `zune_finalize` currently returns 0 for a non-NULL device even if its vendor
  operations fail. It is a best-effort re-index request, not proof of persistence.
- `zune_get_track_state` succeeds if at least one requested playcount/rating
  property was read. Failed fields become zero; skip count is always zero in
  this MTP path. Use the scan's ZMDB fields when available.

Read `zune_get_error` on the calling thread immediately after a failure. Some
failure paths only log and do not set that string, so it can be empty or stale.
`zune_autopsy` exposes the last PTP response, not an immutable error history:
0x2001 is OK, 0x2005 unsupported operation, 0x200C full storage, 0x02FF transport
failure, and 0 means no response. Cleanup or later calls can replace it.

## Read-only quick start

```c
#include "zune.h"
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) {
        fprintf(stderr, "Breach failed: %s\n", zune_get_error());
        return 1;
    }
    const char *name = zune_get_name(dev);
    printf("Connected: %s, free: %" PRIu64 " bytes\n",
           name ? name : "Zune", zune_get_headroom(dev));
    ZuneDBLibrary *library = NULL;
    int result = zune_infiltrate(dev, &library);
    if (result == 0) {
        printf("%d tracks, %d videos, %d photos\n",
               library->track_count, library->video_count, library->photo_count);
    } else {
        fprintf(stderr, "Scan failed: %s (PTP 0x%04x)\n",
                zune_get_error(), (unsigned)zune_autopsy(dev));
    }
    zune_sever(dev);
    zune_free_scan(library);
    return result == 0 ? 0 : 1;
}
```

Save as `example.c` at the repository root, build the library, then on Linux:

```bash
cc -std=c99 -Wall -Wextra -Werror -Iinclude example.c ./libzune.a \
  $(pkg-config --libs libusb-1.0 libgcrypt libavformat libavutil) -o example
```

This reads device information and library data; it does not transfer or delete.

## Choosing operations

**Scanning:** `zune_infiltrate` reads ZMDB and includes an internal MTP playlist
fallback when no playlists were decoded. It is not universally one USB operation
or a complete read of every property. Classic media sizes and some user-state
fields can be unavailable. Artwork is fetched separately. The legacy scan API
returns only tracks/videos/photos; new clients should use `ZuneDBLibrary`.

**Duplicates:** `zune_find_track` compares title, artist and album with
`strcasecmp`; it does not compare disc, track number, file content or edition
IDs. Video/photo lookup compares the legacy `filename` field. For ZMDB videos
that field is a display title, not a guaranteed transport filename. These are
simple search helpers, not a safe universal dedup policy. Applications must
resolve ambiguity and preserve album editions themselves.

**Uploads:** track/video uploads send prepared compatible media; they do not
transcode automatically. Music display also requires album/artist abstract
objects and references. `zune_forge_artist` reuses a live matching artist and
refuses creation after an incomplete inventory read; it does not remove existing
duplicates. `zune_rewire_album` sets the supplied references, not an automatic
union with existing membership. Compose the desired complete list first.
Refresh storage before transfers. The transfer stack cannot send objects larger
than its 32-bit compressed-size limit. A failed or cancelled upload needs
inspection before retry; do not infer that no partial object exists.

**Video:** prefer `zune_smuggle_video_named` for independently controlled display
title and transport filename, with explicit partial-metadata status. The older
movie/episode/clip/other APIs remain compatible but ignore metadata/art failures
after upload; the episode helper synthesizes a series/episode display title.
See [VIDEO_TITLES.md](VIDEO_TITLES.md).

**Preparation:** audio/video arm helpers and `zune_retag` invoke a hard-coded
`/opt/homebrew/bin/ffmpeg`; they are not portable Linux transcoders as written.
`zune_arm_photo` uses `sips` on macOS and `ffmpeg` from PATH on Linux.
`zune_smuggle_photo` invokes that preparation automatically. Thumbnail utilities
also require external tools. Temporary names are not suitable for concurrent
same-input preparation; callers should serialize these legacy helpers.
`USE_LIBAV` probing is in-process; the fallback invokes `ffprobe`.

## Data types

Exact layouts and enums live in [zune.h](../include/zune.h). Rebuild consumers
when these structs change; this project does not promise a stable binary ABI.

| Type | Purpose and important fields |
|---|---|
| `ZuneDeviceHandle` | Opaque device/session handle. |
| `ZuneModel` | Video profile family: 30, 80/flash, HD, HD 720p, unknown. |
| `ZuneDeviceFamily` | Keel 0x00, Scorpius 0x02, Draco 0x03, Pavo 0x06, unknown 0xFF. |
| `ZuneTrack` | ID, text metadata, duration/size, track/disc, playcount/rating/skip count/last played. Zero can mean unavailable; last played uses Windows FILETIME. |
| `ZuneVideoFile` | ID, legacy filename, size, metagenre, parent, explicit title, object_filename and object_format. ZMDB object_filename can be empty. |
| `ZunePhotoFile` | ID, filename, size and parent ID. |
| `ZunePhotoAlbum` | Folder ID, name and photo count. |
| `ZunePlaylist` | ID, name, ordered track IDs and count. |
| `ZuneAlbumObject` | ID, name, artist, track IDs and count. |
| `ZuneDBAlbum` | ID, title, artist, year (HD; zero on Classic). |
| `ZuneDBArtist` | ID and name, not a portrait or online provider identity. |
| `ZuneDBPhotoAlbum` | ID and name. |
| `ZuneDBLibrary` | Arrays/counts for all seven media/object categories, is_hd and scan_ms. |
| `ZuneLibrary` | Deprecated tracks/videos/photos-only scan result. |
| `ZuneMetadata` | title, artist, albumartist, album, genre, tracknumber, duration_ms, discnumber and year. Missing text is NULL, missing numbers zero. |
| `ZuneFolderEntry` | ID and allocated name. |
| `ZuneMetaGenre` | Other 0x21, music video 0x23, movie 0x25, TV 0x26. |
| `zune_progress_fn` | `(uint64_t bytes_sent, uint64_t bytes_total, void *userdata)`. Runs in the synchronous transfer call; do not reenter device I/O. |

## Maintaining this reference

Run `node tools/update-api-reference.mjs` after changing the public header and
review the operation notes against implementation changes. Use `--check` to
validate generated signatures and one note per declared function. Then run
`node tools/update-toc.mjs`. No authentication data is read by either generator.

<!-- GENERATED API START -->

## Complete function reference

90 declarations, in public-header order.

### zune_breach

```c
ZuneDeviceHandle zune_breach(void);
```

Open USB/session and attempt MTPZ authentication if available. Returns a handle or NULL; a non-NULL handle does not prove authentication succeeded. No library scan is performed.

[Declaration](../include/zune.h#L57); [implementation index](TOC.md#public-api).

### zune_sever

```c
void zune_sever(ZuneDeviceHandle dev);
```

Close session/backend and free the handle. NULL is accepted. Caller must first stop/join every operation using it; the internal delay is not synchronization.

[Declaration](../include/zune.h#L62); [implementation index](TOC.md#public-api).

### zune_get_name

```c
const char *zune_get_name(ZuneDeviceHandle dev);
```

Borrowed friendly-name string, or NULL for a NULL handle.

[Declaration](../include/zune.h#L66); [implementation index](TOC.md#public-api).

### zune_get_model

```c
const char *zune_get_model(ZuneDeviceHandle dev);
```

Borrowed model string, or NULL for a NULL handle.

[Declaration](../include/zune.h#L67); [implementation index](TOC.md#public-api).

### zune_get_serial

```c
const char *zune_get_serial(ZuneDeviceHandle dev);
```

Borrowed serial string, or NULL for a NULL handle.

[Declaration](../include/zune.h#L68); [implementation index](TOC.md#public-api).

### zune_get_battery

```c
uint8_t     zune_get_battery(ZuneDeviceHandle dev);
```

Connect-time cached battery percentage; zero for a NULL handle or unavailable reading.

[Declaration](../include/zune.h#L69); [implementation index](TOC.md#public-api).

### zune_get_capacity

```c
uint64_t    zune_get_capacity(ZuneDeviceHandle dev);
```

Cached total storage bytes; refreshed by refresh_storage. Zero for NULL handle.

[Declaration](../include/zune.h#L70); [implementation index](TOC.md#public-api).

### zune_get_headroom

```c
uint64_t    zune_get_headroom(ZuneDeviceHandle dev);
```

Cached free storage bytes; refreshed by refresh_storage. Zero for NULL handle.

[Declaration](../include/zune.h#L71); [implementation index](TOC.md#public-api).

### zune_refresh_storage

```c
int         zune_refresh_storage(ZuneDeviceHandle dev);
```

Read live storage info, update total/free cache on success. 0 success, -1 failure; failure preserves old cached values.

[Declaration](../include/zune.h#L77); [implementation index](TOC.md#public-api).

### zune_identify

```c
ZuneModel   zune_identify(ZuneDeviceHandle dev);
```

Return model/profile family inferred from device family/model data, or UNKNOWN. HD 720p is a caller-selected profile rather than a separate device family.

[Declaration](../include/zune.h#L78); [implementation index](TOC.md#public-api).

### zune_get_family

```c
ZuneDeviceFamily zune_get_family(ZuneDeviceHandle dev);
```

Return cached device family or UNKNOWN when unavailable.

[Declaration](../include/zune.h#L83); [implementation index](TOC.md#public-api).

### zune_is_hdd

```c
int zune_is_hdd(ZuneDeviceHandle dev);
```

Return 1 for Keel/Draco, otherwise 0; unknown is not evidence of flash storage.

[Declaration](../include/zune.h#L87); [implementation index](TOC.md#public-api).

### zune_rename

```c
int zune_rename(ZuneDeviceHandle dev, const char *new_name);
```

Set the device friendly name. 0 success, -1 failure.

[Declaration](../include/zune.h#L90); [implementation index](TOC.md#public-api).

### zune_is_live

```c
int zune_is_live(ZuneDeviceHandle dev);
```

Return 1 if local handle/backend state looks connected, else 0. Does not probe USB responsiveness.

[Declaration](../include/zune.h#L94); [implementation index](TOC.md#public-api).

### zune_abort

```c
void zune_abort(ZuneDeviceHandle dev);
```

Request cooperative cancellation. Wait for the active operation to return before teardown; cancellation does not promise rollback of device objects.

[Declaration](../include/zune.h#L98); [implementation index](TOC.md#public-api).

### zune_is_aborted

```c
int zune_is_aborted(ZuneDeviceHandle dev);
```

Return the cancellation flag, or 0 for NULL.

[Declaration](../include/zune.h#L101); [implementation index](TOC.md#public-api).

### zune_clear_abort

```c
void zune_clear_abort(ZuneDeviceHandle dev);
```

Clear cancellation before a new operation, after the old operation has finished.

[Declaration](../include/zune.h#L104); [implementation index](TOC.md#public-api).

### zune_autopsy

```c
uint16_t zune_autopsy(ZuneDeviceHandle dev);
```

Return the last PTP response code, including synthetic transport failure 0x02FF; 0 for NULL/no response. Subsequent operations can replace it.

[Declaration](../include/zune.h#L111); [implementation index](TOC.md#public-api).

### zune_autopsy_name

```c
const char *zune_autopsy_name(uint16_t code);
```

Return a borrowed static name for a PTP response code, including a fallback for unknown codes.

[Declaration](../include/zune.h#L114); [implementation index](TOC.md#public-api).

### zune_unjam

```c
int zune_unjam(ZuneDeviceHandle dev);
```

Ask the backend to clear both USB endpoints. 0 success, -1 failure; not a guarantee that the device transaction was rolled back or can be retried.

[Declaration](../include/zune.h#L119); [implementation index](TOC.md#public-api).

### zune_forge_folder

```c
uint32_t zune_forge_folder(ZuneDeviceHandle dev, const char *name,
                             uint32_t parent_id);
```

Create a folder in primary storage under parent_id. Return nonzero ID or 0 on failure.

[Declaration](../include/zune.h#L123); [implementation index](TOC.md#public-api).

### zune_get_folders

```c
int zune_get_folders(ZuneDeviceHandle dev, ZuneFolderEntry **out);
```

Enumerate folder ID/name entries. Return count or -1; release the allocated output using free_folders.

[Declaration](../include/zune.h#L129); [implementation index](TOC.md#public-api).

### zune_free_folders

```c
void zune_free_folders(ZuneFolderEntry *entries, int count);
```

Release folder entry names and their array with the returned count.

[Declaration](../include/zune.h#L130); [implementation index](TOC.md#public-api).

### zune_get_tracks

```c
ZuneTrack *zune_get_tracks(ZuneDeviceHandle dev, int *count);
```

Enumerate tracks through MTP into an owned array and count. NULL can mean empty/error; unreadable objects can be skipped.

[Declaration](../include/zune.h#L151); [implementation index](TOC.md#public-api).

### zune_free_tracks

```c
void zune_free_tracks(ZuneTrack *tracks, int count);
```

Release a separately returned track array and strings. Do not use on a subarray owned by a scan you will also free.

[Declaration](../include/zune.h#L152); [implementation index](TOC.md#public-api).

### zune_smuggle_track

```c
int zune_smuggle_track(ZuneDeviceHandle dev, const char *filepath,
                    uint32_t *out_item_id);
```

Probe source metadata, then upload audio bytes. Does not transcode or create the album/artist hierarchy. 0 success, -1 failure; optional output receives ID on success.

[Declaration](../include/zune.h#L156); [implementation index](TOC.md#public-api).

### zune_smuggle_track_tagged

```c
int zune_smuggle_track_tagged(ZuneDeviceHandle dev, const char *filepath,
                               const char *title, const char *artist,
                               const char *album, const char *genre,
                               uint16_t tracknumber, uint32_t duration_ms,
                               uint32_t *out_item_id);
```

Upload audio using explicit title/artist/album/genre/track/duration values. Does not transcode. 0 success, -1 failure.

[Declaration](../include/zune.h#L160); [implementation index](TOC.md#public-api).

### zune_purge_track

```c
int zune_purge_track(ZuneDeviceHandle dev, uint32_t item_id);
```

Delete one device object. 0 success, -1 failure; caller must ensure the ID is the intended track.

[Declaration](../include/zune.h#L167); [implementation index](TOC.md#public-api).

### zune_extract_track

```c
int zune_extract_track(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);
```

Download an object to dest_path. 0 success, -1 failure; inspect/remove partial output after failure as appropriate.

[Declaration](../include/zune.h#L170); [implementation index](TOC.md#public-api).

### zune_get_track_state

```c
int zune_get_track_state(ZuneDeviceHandle dev, uint32_t item_id,
                          uint16_t *out_playcount, uint8_t *out_rating,
                          uint32_t *out_skip_count);
```

Read requested playcount/rating. 0 if at least one was read, -1 otherwise; failed fields become zero and skip count always becomes zero. Output pointers may be NULL.

[Declaration](../include/zune.h#L178); [implementation index](TOC.md#public-api).

### zune_set_track_state

```c
int zune_set_track_state(ZuneDeviceHandle dev, uint32_t item_id,
                          uint16_t playcount, uint8_t rating);
```

Write playcount and rating (0 neutral, 8 liked, 3 disliked). 0 if both writes succeed, -1 if either fails; partial updates are possible.

[Declaration](../include/zune.h#L185); [implementation index](TOC.md#public-api).

### zune_verify

```c
int zune_verify(ZuneDeviceHandle dev, uint32_t item_id,
                 const char *expected_title, const char *expected_artist,
                 uint64_t expected_size);
```

Read back size/title/artist and compare. NULL text skips that check; size 0 skips size. 0 if enabled checks pass, -1 otherwise. Does not compare file content or playback.

[Declaration](../include/zune.h#L193); [implementation index](TOC.md#public-api).

### zune_rename_item

```c
int zune_rename_item(ZuneDeviceHandle dev, uint32_t item_id, const char *new_name);
```

Set object Name (display title), preserving ObjectFileName. 0 success, -1 failure. Does not retag the media file.

[Declaration](../include/zune.h#L200); [implementation index](TOC.md#public-api).

### zune_sync_notify

```c
int zune_sync_notify(ZuneDeviceHandle dev, const char *name, uint32_t op_kind,
                     uint32_t item_index, uint32_t total_items,
                     uint32_t progress_before, uint32_t progress_after);
```

Best-effort on-device batch notification. op_kind 0 upload, 1 download; item_index is 1-based, progress fields are percentages 0..100. 0 success, -1 failure.

[Declaration](../include/zune.h#L213); [implementation index](TOC.md#public-api).

### zune_probe_object

```c
int zune_probe_object(ZuneDeviceHandle dev, uint32_t item_id,
                      uint64_t *out_size, uint16_t *out_format);
```

GetObjectInfo existence/size/format probe. Optional size/format outputs. 0 success, -1 failure; transport failure is not proof the object does not exist.

[Declaration](../include/zune.h#L221); [implementation index](TOC.md#public-api).

### zune_probe_object_named

```c
int zune_probe_object_named(ZuneDeviceHandle dev, uint32_t item_id,
                            uint64_t *out_size, uint16_t *out_format,
                            char **out_name);
```

Object probe plus allocated ObjectFileName, when requested/available. Caller frees name. 0 success, -1 failure.

[Declaration](../include/zune.h#L225); [implementation index](TOC.md#public-api).

### zune_get_item_refs

```c
int zune_get_item_refs(ZuneDeviceHandle dev, uint32_t item_id,
                       uint32_t **out_refs, int *out_count);
```

Read object reference IDs into an allocated array and count. Caller frees the array. 0 success, -1 failure.

[Declaration](../include/zune.h#L231); [implementation index](TOC.md#public-api).

### zune_find_track

```c
uint32_t zune_find_track(ZuneDeviceHandle dev,
                          const char *title, const char *artist,
                          const char *album);
```

Search the live borrowed scan snapshot by case-insensitive title+artist+album. Return first ID or 0. No disc/track/content matching; keep scan allocated.

[Declaration](../include/zune.h#L241); [implementation index](TOC.md#public-api).

### zune_find_video

```c
uint32_t zune_find_video(ZuneDeviceHandle dev, const char *filename);
```

Search the borrowed scan's legacy filename field case-insensitively; that field can be a ZMDB display title. Return first ID or 0. Keep scan allocated.

[Declaration](../include/zune.h#L248); [implementation index](TOC.md#public-api).

### zune_find_photo

```c
uint32_t zune_find_photo(ZuneDeviceHandle dev, const char *filename);
```

Search the borrowed scan's photo filename case-insensitively. Return first ID or 0. Keep scan allocated; no folder/content disambiguation.

[Declaration](../include/zune.h#L252); [implementation index](TOC.md#public-api).

### zune_get_videos

```c
ZuneVideoFile *zune_get_videos(ZuneDeviceHandle dev, int *count);
```

Enumerate videos via MTP into an owned array and count. Explicit title/ObjectFileName fields distinguish display from transport names. NULL can mean empty/error.

[Declaration](../include/zune.h#L277); [implementation index](TOC.md#public-api).

### zune_free_videos

```c
void zune_free_videos(ZuneVideoFile *videos, int count);
```

Release a separately allocated video array, including filename, title and object_filename strings.

[Declaration](../include/zune.h#L278); [implementation index](TOC.md#public-api).

### zune_smuggle_video_named

```c
int zune_smuggle_video_named(ZuneDeviceHandle dev, const char *filepath,
                            const char *object_filename, const char *title,
                            uint16_t meta_genre,
                            const char *series, int season, int episode,
                            const char *description,
                            const uint8_t *poster_jpeg, size_t poster_len,
                            uint32_t *out_item_id);
```

Preferred video upload. Independent transport filename and title; requires non-NULL out_item_id. 0 complete, 1 media uploaded but metadata/art incomplete, -1 validation/file-transfer failure. For 0/1 returned ID is valid. TV series properties apply only for TV genre and nonempty series; season/episode must be nonnegative. Never reupload for result 1.

[Declaration](../include/zune.h#L296); [implementation index](TOC.md#public-api).

### zune_smuggle_movie

```c
int zune_smuggle_movie(ZuneDeviceHandle dev, const char *filepath,
                     const char *display_name, const char *description,
                     const uint8_t *poster_jpeg, size_t poster_len,
                     uint32_t *out_item_id);
```

Legacy movie upload; display_name is used for transport/display. 0 means media upload succeeded, even if later metadata/poster failed. Prefer smuggle_video_named.

[Declaration](../include/zune.h#L310); [implementation index](TOC.md#public-api).

### zune_get_series_info

```c
int zune_get_series_info(ZuneDeviceHandle dev, uint32_t item_id,
                                char **out_series, int *out_season, int *out_episode,
                                char **out_title);
```

Read series/season/episode and optional title (Name, then filename fallback). 0 when nonempty series found, -1 otherwise. Caller frees returned series/title strings, including outputs returned on a failed series lookup.

[Declaration](../include/zune.h#L318); [implementation index](TOC.md#public-api).

### zune_smuggle_episode

```c
int zune_smuggle_episode(ZuneDeviceHandle dev, const char *filepath,
                          const char *display_name,
                          const char *series, int season, int episode,
                          const char *description,
                          const uint8_t *poster_jpeg, size_t poster_len,
                          uint32_t *out_item_id);
```

Legacy TV upload; synthesizes Series - SxxEyy title and only sets series data for positive episode. 0 does not certify metadata/art. Prefer smuggle_video_named.

[Declaration](../include/zune.h#L325); [implementation index](TOC.md#public-api).

### zune_smuggle_clip

```c
int zune_smuggle_clip(ZuneDeviceHandle dev, const char *filepath,
                           const char *display_name,
                           const uint8_t *poster_jpeg, size_t poster_len,
                           uint32_t *out_item_id);
```

Legacy music-video upload; display_name used for transport/display. 0 does not certify metadata/art. Prefer smuggle_video_named.

[Declaration](../include/zune.h#L333); [implementation index](TOC.md#public-api).

### zune_smuggle_other

```c
int zune_smuggle_other(ZuneDeviceHandle dev, const char *filepath,
                       const char *display_name, const char *description,
                       const uint8_t *poster_jpeg, size_t poster_len,
                       uint32_t *out_item_id);
```

Legacy Other video upload; display_name used for transport/display. 0 does not certify metadata/art. Prefer smuggle_video_named.

[Declaration](../include/zune.h#L339); [implementation index](TOC.md#public-api).

### zune_purge_video

```c
int zune_purge_video(ZuneDeviceHandle dev, uint32_t item_id);
```

Delete a video object. 0 success, -1 failure.

[Declaration](../include/zune.h#L345); [implementation index](TOC.md#public-api).

### zune_extract_video

```c
int zune_extract_video(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);
```

Download a video object to dest_path. 0 success, -1 failure. No direct playback API is provided.

[Declaration](../include/zune.h#L348); [implementation index](TOC.md#public-api).

### zune_get_photos

```c
ZunePhotoFile *zune_get_photos(ZuneDeviceHandle dev, int *count);
```

Enumerate photos via MTP into an allocated array and count. NULL can mean empty/error.

[Declaration](../include/zune.h#L367); [implementation index](TOC.md#public-api).

### zune_free_photos

```c
void zune_free_photos(ZunePhotoFile *photos, int count);
```

Release a separately returned photo array and strings.

[Declaration](../include/zune.h#L368); [implementation index](TOC.md#public-api).

### zune_get_dimensions

```c
int zune_get_dimensions(ZuneDeviceHandle dev, uint32_t item_id,
                               uint32_t *width, uint32_t *height);
```

Read width/height MTP properties. Supply both output pointers. 0 success, -1 failure.

[Declaration](../include/zune.h#L372); [implementation index](TOC.md#public-api).

### zune_get_photo_albums

```c
ZunePhotoAlbum *zune_get_photo_albums(ZuneDeviceHandle dev,
                                       ZunePhotoFile *photos, int photo_count,
                                       int *album_count);
```

Group supplied photos by parent folder and resolve names. Returned folder albums are allocated; input photos remain caller-owned. Use free_photo_albums.

[Declaration](../include/zune.h#L376); [implementation index](TOC.md#public-api).

### zune_free_photo_albums

```c
void zune_free_photo_albums(ZunePhotoAlbum *albums, int count);
```

Release allocated photo-album names and array.

[Declaration](../include/zune.h#L379); [implementation index](TOC.md#public-api).

### zune_arm_photo

```c
char *zune_arm_photo(const char *input_path);
```

Prepare a max-480px JPEG via sips (macOS) or ffmpeg on PATH (Linux). Return owned temporary path or NULL. Unlink the file and free the path; serialize calls to avoid temporary-name collisions.

[Declaration](../include/zune.h#L384); [implementation index](TOC.md#public-api).

### zune_smuggle_photo

```c
int zune_smuggle_photo(ZuneDeviceHandle dev, const char *filepath,
                     const char *album_name);
```

Prepare JPEG automatically and send to a named album folder, creating it when absent; NULL album sends to root. 0 success, -1 failure. No uploaded-ID output is exposed.

[Declaration](../include/zune.h#L389); [implementation index](TOC.md#public-api).

### zune_purge_photo

```c
int zune_purge_photo(ZuneDeviceHandle dev, uint32_t item_id);
```

Delete a photo object. 0 success, -1 failure.

[Declaration](../include/zune.h#L393); [implementation index](TOC.md#public-api).

### zune_extract_photo

```c
int zune_extract_photo(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);
```

Download photo to dest_path. 0 success, -1 failure.

[Declaration](../include/zune.h#L396); [implementation index](TOC.md#public-api).

### zune_get_playlists

```c
ZunePlaylist *zune_get_playlists(ZuneDeviceHandle dev, int *count);
```

Enumerate playlists and references through MTP. Owned array/count; NULL can mean empty/error. Unreadable entries or references can yield partial results.

[Declaration](../include/zune.h#L409); [implementation index](TOC.md#public-api).

### zune_free_playlists

```c
void zune_free_playlists(ZunePlaylist *playlists, int count);
```

Release a separately returned playlist array, names and track-ID arrays.

[Declaration](../include/zune.h#L410); [implementation index](TOC.md#public-api).

### zune_forge_playlist

```c
uint32_t zune_forge_playlist(ZuneDeviceHandle dev, const char *name,
                               uint32_t *track_ids, int count);
```

Create an abstract playlist with supplied track IDs/order. Return nonzero object ID or 0. Does not upload tracks.

[Declaration](../include/zune.h#L413); [implementation index](TOC.md#public-api).

### zune_rewire_playlist

```c
int zune_rewire_playlist(ZuneDeviceHandle dev, uint32_t playlist_id,
                          const char *name,
                          uint32_t *track_ids, int count);
```

Update playlist name and supplied membership/order. 0 success, -1 failure. This is not an automatic append or transaction across both fields.

[Declaration](../include/zune.h#L417); [implementation index](TOC.md#public-api).

### zune_purge_playlist

```c
int zune_purge_playlist(ZuneDeviceHandle dev, uint32_t playlist_id);
```

Delete the playlist object, not its referenced media. 0 success, -1 failure.

[Declaration](../include/zune.h#L422); [implementation index](TOC.md#public-api).

### zune_forge_album

```c
uint32_t zune_forge_album(ZuneDeviceHandle dev,
                                   const char *album_name,
                                   const char *artist_name,
                                   const char *genre,
                                   uint32_t *track_ids, int track_count);
```

Create an abstract music album and set supplied references. Return nonzero ID or 0. Create/reuse and link artist separately; inspect logs/readback for ancillary metadata/reference failures.

[Declaration](../include/zune.h#L433); [implementation index](TOC.md#public-api).

### zune_rewire_album

```c
int zune_rewire_album(ZuneDeviceHandle dev, uint32_t album_id,
                              const char *album_name,
                              const char *artist_name,
                              uint32_t *track_ids, int track_count);
```

Update album names and replace references with the supplied complete list when nonempty. Caller performs any merge. Empty input does not clear references. Name/artist failures only log warnings, so 0 does not certify metadata; -1 reports invalid device/reference failure.

[Declaration](../include/zune.h#L442); [implementation index](TOC.md#public-api).

### zune_forge_artist

```c
uint32_t zune_forge_artist(ZuneDeviceHandle dev,
                                    const char *artist_name);
```

Reuse a live matching artist, otherwise create a zero-byte abstract artist. Return ID or 0; failed inventory blocks creation. ASCII case/outer-space matching preserves UTF-8 bytes; no duplicate cleanup.

[Declaration](../include/zune.h#L452); [implementation index](TOC.md#public-api).

### zune_link_artist

```c
int zune_link_artist(ZuneDeviceHandle dev, uint32_t object_id,
                        uint32_t artist_handle);
```

Write ArtistId for an album/track. 0 success, -1 failure.

[Declaration](../include/zune.h#L456); [implementation index](TOC.md#public-api).

### zune_get_albums

```c
ZuneAlbumObject *zune_get_albums(ZuneDeviceHandle dev, int *count);
```

Enumerate abstract albums, names, artists and references via MTP. Owned array/count; NULL can mean empty/error; property failures can produce incomplete records.

[Declaration](../include/zune.h#L468); [implementation index](TOC.md#public-api).

### zune_free_albums

```c
void zune_free_albums(ZuneAlbumObject *albums, int count);
```

Release album array, strings and track-ID arrays returned by get_albums.

[Declaration](../include/zune.h#L469); [implementation index](TOC.md#public-api).

### zune_brand

```c
int zune_brand(ZuneDeviceHandle dev, uint32_t item_id,
                        const uint8_t *jpeg_data, size_t jpeg_len);
```

Write JPEG representative-sample artwork to an object. 0 success, -1 failure; image is not automatically resized by this API.

[Declaration](../include/zune.h#L475); [implementation index](TOC.md#public-api).

### zune_grab_thumb

```c
int zune_grab_thumb(ZuneDeviceHandle dev, uint32_t item_id,
                        const char *cache_path);
```

Try representative sample then MTP thumbnail, writing cache_path. 0 success, -1 failure. Does not download the complete media file.

[Declaration](../include/zune.h#L481); [implementation index](TOC.md#public-api).

### zune_grab_photo_thumb

```c
int zune_grab_photo_thumb(ZuneDeviceHandle dev, uint32_t item_id,
                              const char *cache_path);
```

Photo-only fallback: representative sample, MTP thumbnail, then complete photo download/resize. 0 success, -1 failure. Do not use as a video thumbnail API.

[Declaration](../include/zune.h#L487); [implementation index](TOC.md#public-api).

### zune_arm_audio

```c
char *zune_arm_audio(const char *input_path);
```

Legacy MP3 320kbps/ID3v2.3 preparation, with source metadata stripped for separate MTP tagging. Hard-coded Homebrew ffmpeg path. Owned temporary path or NULL; unlink and free.

[Declaration](../include/zune.h#L496); [implementation index](TOC.md#public-api).

### zune_retag

```c
char *zune_retag(const char *mp3_path);
```

Legacy MP3 stream-copy retag to ID3v2.3. Hard-coded Homebrew ffmpeg path. Owned temporary path or NULL; unlink and free.

[Declaration](../include/zune.h#L501); [implementation index](TOC.md#public-api).

### zune_arm_video

```c
char *zune_arm_video(const char *input_path, ZuneModel model);
```

Legacy transcode by ZuneModel profile (WMV for 30, H.264 for other supported profiles). Hard-coded Homebrew ffmpeg path. Owned temporary path or NULL; unlink and free.

[Declaration](../include/zune.h#L506); [implementation index](TOC.md#public-api).

### zune_arm_episode

```c
char *zune_arm_episode(const char *input_path, ZuneModel model,
                                 const char *series, int season, int episode);
```

Legacy video preparation with series/season/episode container metadata. Same portability/temp ownership limits as arm_video.

[Declaration](../include/zune.h#L510); [implementation index](TOC.md#public-api).

### zune_infiltrate_legacy

```c
int zune_infiltrate_legacy(ZuneDeviceHandle dev, ZuneLibrary **out);
```

Deprecated tracks/videos/photos-only ZMDB result. 0 success, -1 failure. Release with free_library; prefer infiltrate.

[Declaration](../include/zune.h#L526); [implementation index](TOC.md#public-api).

### zune_free_library

```c
void zune_free_library(ZuneLibrary *lib);
```

Release a deprecated ZuneLibrary and its owned arrays/strings.

[Declaration](../include/zune.h#L527); [implementation index](TOC.md#public-api).

### zune_finalize

```c
int zune_finalize(ZuneDeviceHandle dev);
```

Issue 0x9201, 0x9108 and 0x9202 re-index-related requests. Returns -1 for NULL, otherwise 0 regardless of individual operation results. Verify persistence after device re-index.

[Declaration](../include/zune.h#L534); [implementation index](TOC.md#public-api).

### zune_decode_filename

```c
int zune_decode_filename(const char *filepath,
                               char **out_series, int *out_season,
                               int *out_episode);
```

Heuristically parse series/season/episode from a path. 1 pattern found, 0 absent/invalid. Caller frees allocated series. Not an online identity resolver.

[Declaration](../include/zune.h#L541); [implementation index](TOC.md#public-api).

### zune_snap_thumb

```c
int zune_snap_thumb(const char *video_path, const char *output_path);
```

Use ffmpeg from PATH to grab a frame at 10 seconds and scale to 200px width. 0 success, -1 failure; short media may have no frame there.

[Declaration](../include/zune.h#L547); [implementation index](TOC.md#public-api).

### zune_probe

```c
int zune_probe(const char *filepath, ZuneMetadata *out);
```

Inspect tags using native libavformat (USE_LIBAV) or ffprobe fallback. 0 inspected, -1 failure with zeroed output. Missing tags are not failure. Container nonblank tags precede selected-stream tags. Free an old result before reusing the struct.

[Declaration](../include/zune.h#L571); [implementation index](TOC.md#public-api).

### zune_free_metadata

```c
void zune_free_metadata(ZuneMetadata *meta);
```

Free allocated fields within the caller's metadata struct, not the struct itself.

[Declaration](../include/zune.h#L572); [implementation index](TOC.md#public-api).

### zune_smuggle_track_ex

```c
int zune_smuggle_track_ex(ZuneDeviceHandle dev, const char *filepath,
                        const char *title, const char *artist,
                        const char *album, const char *genre,
                        uint16_t tracknumber, uint32_t duration_ms,
                        zune_progress_fn progress, void *userdata,
                        uint32_t *out_item_id);
```

Explicit-metadata audio upload with synchronous byte-progress callback and userdata. 0 success, -1 failure. Do not reenter device I/O in the callback; caller controls worker dispatch.

[Declaration](../include/zune.h#L581); [implementation index](TOC.md#public-api).

### zune_infiltrate

```c
int zune_infiltrate(ZuneDeviceHandle dev, ZuneDBLibrary **out);
```

Read/parse ZMDB and fall back internally to MTP playlists if none decoded. 0 success, -1 failure. Owns returned scan; device borrows it for find helpers. Does not prove all object sizes or fields are known.

[Declaration](../include/zune.h#L628); [implementation index](TOC.md#public-api).

### zune_free_scan

```c
void zune_free_scan(ZuneDBLibrary *lib);
```

Release the scan and every nested array/string. Does not invalidate the borrowed device cache; never use find helpers afterward until another successful scan.

[Declaration](../include/zune.h#L631); [implementation index](TOC.md#public-api).

### zune_dump_raw

```c
int zune_dump_raw(ZuneDeviceHandle dev, const char *output_path);
```

Write the raw ZMDB response to output_path for research. 0 success, -1 failure. Output can contain personal library metadata.

[Declaration](../include/zune.h#L637); [implementation index](TOC.md#public-api).

### zune_infiltrate_deep

```c
int zune_infiltrate_deep(ZuneDeviceHandle dev);
```

Research-only descriptor scan/logging, including sample record dumps. 0 success, -1 failure; logs can contain personal library metadata.

[Declaration](../include/zune.h#L642); [implementation index](TOC.md#public-api).

### zune_get_error

```c
const char *zune_get_error(void);
```

Borrowed thread-local diagnostic string. Read on the failing call's thread; not all error paths update it and successful operations need not clear it.

[Declaration](../include/zune.h#L648); [implementation index](TOC.md#public-api).
