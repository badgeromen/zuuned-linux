/*
 * libzune — Zune MTP Device Library
 *
 * The first open-source library for full Zune device management.
 * Handles MTPZ authentication, music/video/photo sync, TV series metadata
 * (vendor properties), UCS-2 descriptions, album art, transcode profiles,
 * and photo album management.
 *
 * Pure C. No GTK. Native PTP/MTP/MTPZ stack (no libmtp dependency).
 *
 * Copyright (c) 2026 BadgerOmens
 */

#ifndef ZUNE_H
#define ZUNE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Version ---- */

#define LIBZUNE_VERSION_MAJOR 0
#define LIBZUNE_VERSION_MINOR 1
#define LIBZUNE_VERSION_PATCH 0

/* ---- Device ---- */

/* ZuneDevice — opaque handle. Use zune_breach() to connect, zune_* to interact. */
struct ZuneDevice;
typedef struct ZuneDevice* ZuneDeviceHandle;

typedef enum {
    ZUNE_MODEL_30,       /* Zune 30GB — WMV only */
    ZUNE_MODEL_80,       /* Zune 4/8/16/80/120 — H.264 320x240 */
    ZUNE_MODEL_HD,       /* Zune HD — H.264 480x272 */
    ZUNE_MODEL_HD_720P,  /* Zune HD — H.264 1280x720 */
    ZUNE_MODEL_UNKNOWN
} ZuneModel;

/* Device family — hardware generation from MTP property 0xD21A.
 * More precise than ZuneModel (distinguishes HDD from flash). */
typedef enum {
    ZUNE_FAMILY_KEEL     = 0x00,  /* 1st gen — Zune 30 (HDD) */
    ZUNE_FAMILY_SCORPIUS = 0x02,  /* 2nd gen flash — Zune 4/8/16 */
    ZUNE_FAMILY_DRACO    = 0x03,  /* 2nd gen HDD — Zune 80/120 */
    ZUNE_FAMILY_PAVO     = 0x06,  /* Zune HD — 16/32/64 */
    ZUNE_FAMILY_UNKNOWN  = 0xFF
} ZuneDeviceFamily;

/* Breach the Zune's defenses. Blocks until MTPZ auth + object cache completes.
 * Returns NULL on failure (no device, auth failure, etc.) */
ZuneDeviceHandle zune_breach(void);

/* Sever the connection and free device. Safe to call while operations pending —
 * signals background threads to stop via disconnecting flag. */
void zune_sever(ZuneDeviceHandle dev);

/* Device info getters */
const char *zune_get_name(ZuneDeviceHandle dev);
const char *zune_get_model(ZuneDeviceHandle dev);
const char *zune_get_serial(ZuneDeviceHandle dev);
uint8_t     zune_get_battery(ZuneDeviceHandle dev);
uint64_t    zune_get_capacity(ZuneDeviceHandle dev);
uint64_t    zune_get_headroom(ZuneDeviceHandle dev);

/* Re-read live free/total space from the device (GetStorageInfo). The
 * get_capacity/get_headroom values are a connect-time snapshot; call this
 * before committing each file during a sync to avoid overcommitting the
 * device (StoreFull). Returns 0 on success, -1 on failure. */
int         zune_refresh_storage(ZuneDeviceHandle dev);
ZuneModel   zune_identify(ZuneDeviceHandle dev);

/* Device family from MTP property 0xD21A. Distinguishes HDD (Keel/Draco)
 * from flash (Scorpius) and HD (Pavo). Returns ZUNE_FAMILY_UNKNOWN if
 * the property is unavailable. */
ZuneDeviceFamily zune_get_family(ZuneDeviceHandle dev);

/* Returns 1 if the device is HDD-based (Keel or Draco), 0 otherwise.
 * Useful for pacing decisions — HDD Zunes need slower I/O cadence. */
int zune_is_hdd(ZuneDeviceHandle dev);

/* Set device friendly name. */
int zune_rename(ZuneDeviceHandle dev, const char *new_name);

/* Check if device is still connected and responsive */
int zune_is_live(ZuneDeviceHandle dev);

/* Request cancellation of in-progress transfer. Thread-safe. */
void zune_abort(ZuneDeviceHandle dev);

/* Check if cancellation was requested. */
int zune_is_aborted(ZuneDeviceHandle dev);

/* Clear cancellation flag (call before starting a new transfer). */
void zune_clear_abort(ZuneDeviceHandle dev);

/* Cause of death for the last operation: the device's PTP response code
 * (0x2001 = OK), 0x02FF for transport-level failures (stall/timeout/short
 * read), or 0 if no response was read. Examine after any failed call to
 * learn WHY it died — 0x200C means the device is full, 0x2005 means the
 * device refused the operation, 0x02FF means the USB pipe is wounded. */
uint16_t zune_autopsy(ZuneDeviceHandle dev);

/* Human-readable name for an autopsy code ("StoreFull", "AccessDenied"…). */
const char *zune_autopsy_name(uint16_t code);

/* Clear USB stall on both endpoints. Call after PIPE/STALL/timeout errors
 * before retrying a transfer. Without this, a single stall causes the
 * entire sync to hang forever on macOS. */
int zune_unjam(ZuneDeviceHandle dev);

/* Create a folder on the device. Returns folder ID or 0 on failure.
 * Uses the device's primary storage. */
uint32_t zune_forge_folder(ZuneDeviceHandle dev, const char *name,
                             uint32_t parent_id);

/* Get folder name mapping (folder_id → name). Caller frees entries and array.
 * Returns number of entries, or -1 on failure. */
typedef struct { uint32_t id; char *name; } ZuneFolderEntry;
int zune_get_folders(ZuneDeviceHandle dev, ZuneFolderEntry **out);
void zune_free_folders(ZuneFolderEntry *entries, int count);

/* ---- Tracks (Music) ---- */

typedef struct {
    uint32_t item_id;
    char *title;
    char *artist;
    char *album;
    char *genre;
    uint32_t duration_ms;
    uint64_t filesize;
    uint16_t tracknumber;
    uint16_t disc_number;     /* From ZMDB varint field 0x6C (0 if unavailable) */
    uint16_t playcount;       /* From ZMDB fixed offset +26 on HD (0 if unavailable) */
    uint8_t  rating;          /* From ZMDB fixed offset +30 on HD. 0=neutral, 8=liked, 3=disliked */
    uint32_t skip_count;      /* From ZMDB varint field 0x63 (0 if unavailable) */
    uint64_t last_played;     /* From ZMDB varint field 0x70. Windows FILETIME (0 if unavailable) */
} ZuneTrack;

/* Get all tracks on device. Caller frees with zune_free_tracks(). */
ZuneTrack *zune_get_tracks(ZuneDeviceHandle dev, int *count);
void zune_free_tracks(ZuneTrack *tracks, int count);

/* Send audio file to device. Metadata extracted from file tags.
 * out_item_id: receives MTP item ID (for album art, etc.) */
int zune_smuggle_track(ZuneDeviceHandle dev, const char *filepath,
                    uint32_t *out_item_id);

/* Send audio file with explicit metadata (overrides file tags). */
int zune_smuggle_track_tagged(ZuneDeviceHandle dev, const char *filepath,
                               const char *title, const char *artist,
                               const char *album, const char *genre,
                               uint16_t tracknumber, uint32_t duration_ms,
                               uint32_t *out_item_id);

/* Delete track from device. */
int zune_purge_track(ZuneDeviceHandle dev, uint32_t item_id);

/* Download track from device to local file. */
int zune_extract_track(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);

/* Read track user state via MTP. Any output pointer may be NULL to skip.
 * Rating encoding: 0=neutral, 8=liked, 3=disliked.
 * Returns 0 on success, -1 on failure. */
int zune_get_track_state(ZuneDeviceHandle dev, uint32_t item_id,
                          uint16_t *out_playcount, uint8_t *out_rating,
                          uint32_t *out_skip_count);

/* Write track playcount and rating to device via MTP.
 * Rating encoding: 0=neutral, 8=liked, 3=disliked.
 * Returns 0 on success, -1 on failure. */
int zune_set_track_state(ZuneDeviceHandle dev, uint32_t item_id,
                          uint16_t playcount, uint8_t rating);

/* Verify a track after upload by reading back properties from the device.
 * Checks title, artist, and filesize match expected values.
 * Returns 0 if all checks pass, -1 if any mismatch or read failure.
 * Pass NULL for expected_title/expected_artist to skip that check.
 * Pass 0 for expected_size to skip size check. */
int zune_verify(ZuneDeviceHandle dev, uint32_t item_id,
                 const char *expected_title, const char *expected_artist,
                 uint64_t expected_size);

/* ---- Rename ----
 * Set the display Name (0xDC44) of any item — track, video, album,
 * playlist. The Zune UI lists show this string everywhere. */
int zune_rename_item(ZuneDeviceHandle dev, uint32_t item_id, const char *new_name);

/* ---- Sync progress notification (0x922A, wire-confirmed) ----
 * Tell the device the item name + batch progress before a transfer —
 * drives the on-device sync status display. Best-effort.
 *
 * Field semantics from capture-bulkreading.pcapng (1,296 uploads):
 *   op_kind:  0 = host->device write (the official client sends 0 for
 *             every upload), 1 = device->host read. Sending 1 during
 *             a sync is accepted but not displayed.
 *   progress_before/after: PERCENTAGES (0-100) of overall batch
 *             progress, not work-unit counters.
 *   item_index: 1-based position in the batch. */
int zune_sync_notify(ZuneDeviceHandle dev, const char *name, uint32_t op_kind,
                     uint32_t item_index, uint32_t total_items,
                     uint32_t progress_before, uint32_t progress_after);

/* ---- Diagnostics ----
 * Existence + size probe (GetObjectInfo) for auditing ZMDB rows against
 * real objects. Returns 0 if the object exists; -1 if the device can't
 * produce it (stranded DB entry). out_size/out_format may be NULL. */
int zune_probe_object(ZuneDeviceHandle dev, uint32_t item_id,
                      uint64_t *out_size, uint16_t *out_format);
/* As above, plus the object's FILENAME (caller frees; may be NULL).
 * Backfills photo names the Classic ZMDB parse can't resolve. */
int zune_probe_object_named(ZuneDeviceHandle dev, uint32_t item_id,
                            uint64_t *out_size, uint16_t *out_format,
                            char **out_name);

/* Raw GetObjectReferences (0x9810) — playlist/album member lists.
 * Caller frees *out_refs. Returns 0 on success. */
int zune_get_item_refs(ZuneDeviceHandle dev, uint32_t item_id,
                       uint32_t **out_refs, int *out_count);

/* ---- Duplicate Detection ---- */

/* Search cached ZMDB library for an existing track by title+artist+album.
 * Requires a prior zune_infiltrate() call. Case-insensitive matching.
 * Returns item_id if found, 0 if not found or no cached library. */
uint32_t zune_find_track(ZuneDeviceHandle dev,
                          const char *title, const char *artist,
                          const char *album);

/* Search cached ZMDB library for an existing video by filename.
 * Returns item_id if found, 0 if not found. */
uint32_t zune_find_video(ZuneDeviceHandle dev, const char *filename);

/* Search cached ZMDB library for an existing photo by filename.
 * Returns item_id if found, 0 if not found. */
uint32_t zune_find_photo(ZuneDeviceHandle dev, const char *filename);

/* ---- Videos ---- */

typedef enum {
    ZUNE_METAGENRE_OTHER       = 0x21,
    ZUNE_METAGENRE_MUSIC_VIDEO = 0x23,
    ZUNE_METAGENRE_MOVIE       = 0x25,
    ZUNE_METAGENRE_TV_SHOW     = 0x26
} ZuneMetaGenre;

typedef struct {
    uint32_t item_id;
    /* Legacy field: ZMDB display title, but MTP fallback ObjectFileName.
     * Retained for source compatibility; use the explicit fields below. */
    char *filename;
    uint64_t filesize;
    uint16_t metagenre;
    uint32_t parent_id;
    char *title;            /* Name (0xDC44); empty when unavailable. */
    char *object_filename;  /* ObjectFileName (0xDC07); empty in ZMDB scans. */
    uint16_t object_format; /* MTP format code, independent of title/extension. */
} ZuneVideoFile;

/* Get all video files on device (with MetaGenre). Caller frees. */
ZuneVideoFile *zune_get_videos(ZuneDeviceHandle dev, int *count);
void zune_free_videos(ZuneVideoFile *videos, int count);

/* A complete media upload whose requested metadata/art was not all saved.
 * The object exists: do NOT retry the upload. Inspect zune_get_error() and
 * use the returned item ID for targeted repair. */
enum { ZUNE_VIDEO_METADATA_INCOMPLETE = 1 };

/* Send any video category with an independent transport filename and title.
 * object_filename includes its playable extension; title is the exact Name
 * (0xDC44), with no added series, episode numbering, or extension.
 * TV series/season/episode are separate vendor properties. Nonempty series
 * writes all three, including season/episode zero. Numbers must be >= 0.
 * Returns 0 for complete success, ZUNE_VIDEO_METADATA_INCOMPLETE when the
 * media exists but metadata/art failed, or -1 for invalid input/file failure.
 * *out_item_id is nonzero for both completed-upload results, otherwise zero.
 * The caller must provide out_item_id. All returned video strings are owned
 * by their containing result and released by the matching free function.
 * Struct fields were appended: rebuild consumers against this header. */
int zune_smuggle_video_named(ZuneDeviceHandle dev, const char *filepath,
                            const char *object_filename, const char *title,
                            uint16_t meta_genre,
                            const char *series, int season, int episode,
                            const char *description,
                            const uint8_t *poster_jpeg, size_t poster_len,
                            uint32_t *out_item_id);

/* Send a movie to the device. Sets MetaGenre=0x25 (Movie).
 * description: TMDB/user description (UCS-2 encoded internally).
 * poster_jpeg/poster_len: poster art sent as representative sample.
 * Pass NULL/0 for no poster. */
int zune_smuggle_movie(ZuneDeviceHandle dev, const char *filepath,
                     const char *display_name, const char *description,
                     const uint8_t *poster_jpeg, size_t poster_len,
                     uint32_t *out_item_id);

/* Read series/season/episode vendor properties from a TV video on device.
 * out_title is Name (0xDC44), falling back to ObjectFileName if unavailable.
 * out_series/out_title are allocated — caller frees. Returns 0 if series found. */
int zune_get_series_info(ZuneDeviceHandle dev, uint32_t item_id,
                                char **out_series, int *out_season, int *out_episode,
                                char **out_title);

/* Send a TV episode. Sets MetaGenre=0x26 (TV Show) + vendor props.
 * series/season/episode written to Zune vendor properties
 * (0xDA9A, 0xDAB5, 0xDAB6). */
int zune_smuggle_episode(ZuneDeviceHandle dev, const char *filepath,
                          const char *display_name,
                          const char *series, int season, int episode,
                          const char *description,
                          const uint8_t *poster_jpeg, size_t poster_len,
                          uint32_t *out_item_id);

/* Send a music video. Sets MetaGenre=0x23 (Music Video). */
int zune_smuggle_clip(ZuneDeviceHandle dev, const char *filepath,
                           const char *display_name,
                           const uint8_t *poster_jpeg, size_t poster_len,
                           uint32_t *out_item_id);

/* Send an "Other" video. Sets MetaGenre=0x21 (Other). */
int zune_smuggle_other(ZuneDeviceHandle dev, const char *filepath,
                       const char *display_name, const char *description,
                       const uint8_t *poster_jpeg, size_t poster_len,
                       uint32_t *out_item_id);

/* Delete video from device. */
int zune_purge_video(ZuneDeviceHandle dev, uint32_t item_id);

/* Download video from device to local file. */
int zune_extract_video(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);

/* ---- Photos ---- */

typedef struct {
    uint32_t folder_id;
    char *name;
    int photo_count;
} ZunePhotoAlbum;

typedef struct {
    uint32_t item_id;
    char *filename;
    uint64_t filesize;
    uint32_t parent_id;
} ZunePhotoFile;

/* Get all photo files on device. Caller frees. */
ZunePhotoFile *zune_get_photos(ZuneDeviceHandle dev, int *count);
void zune_free_photos(ZunePhotoFile *photos, int count);

/* Get original image dimensions for a photo on device (MTP properties 0xDC87/0xDC88).
 * Returns 0 on success, -1 on failure. */
int zune_get_dimensions(ZuneDeviceHandle dev, uint32_t item_id,
                               uint32_t *width, uint32_t *height);

/* Get photo albums (folders) on device. Caller frees. */
ZunePhotoAlbum *zune_get_photo_albums(ZuneDeviceHandle dev,
                                       ZunePhotoFile *photos, int photo_count,
                                       int *album_count);
void zune_free_photo_albums(ZunePhotoAlbum *albums, int count);

/* Arm a photo for Zune: resize to 480px max, convert to baseline JPEG.
 * Returns path to temp file (caller frees + unlinks). NULL on failure.
 * Called automatically by zune_smuggle_photo(), but exposed for preview/caching. */
char *zune_arm_photo(const char *input_path);

/* Send photo to device, optionally into a named album (folder).
 * album_name=NULL sends to root. Creates album if it doesn't exist.
 * Automatically resizes and converts to JPEG for Zune compatibility. */
int zune_smuggle_photo(ZuneDeviceHandle dev, const char *filepath,
                     const char *album_name);

/* Delete photo from device. */
int zune_purge_photo(ZuneDeviceHandle dev, uint32_t item_id);

/* Download photo from device to local file. */
int zune_extract_photo(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);

/* ---- Playlists ---- */

typedef struct {
    uint32_t playlist_id;
    char *name;
    uint32_t *track_ids;
    uint32_t track_count;
} ZunePlaylist;

/* Get all playlists. Caller frees. */
ZunePlaylist *zune_get_playlists(ZuneDeviceHandle dev, int *count);
void zune_free_playlists(ZunePlaylist *playlists, int count);

/* Create playlist with track IDs. Returns playlist_id or 0 on failure. */
uint32_t zune_forge_playlist(ZuneDeviceHandle dev, const char *name,
                               uint32_t *track_ids, int count);

/* Update existing playlist's track list. */
int zune_rewire_playlist(ZuneDeviceHandle dev, uint32_t playlist_id,
                          const char *name,
                          uint32_t *track_ids, int count);

/* Delete playlist. */
int zune_purge_playlist(ZuneDeviceHandle dev, uint32_t playlist_id);

/* ---- MTP Album/Artist Object Hierarchy ---- */

/* The Zune reads album/artist metadata from MTP abstract objects,
 * not from individual track properties. After sending tracks, create
 * these objects and link them for proper display on the device.
 * Reference: zune-explorer's zune-manager.js _createAlbumObjects() */

/* Create an MTP album object and associate it with the given track IDs.
 * Returns album_id or 0 on failure. */
uint32_t zune_forge_album(ZuneDeviceHandle dev,
                                   const char *album_name,
                                   const char *artist_name,
                                   const char *genre,
                                   uint32_t *track_ids, int track_count);

/* Update an existing album's track list (merge new tracks). */
int zune_rewire_album(ZuneDeviceHandle dev, uint32_t album_id,
                              const char *album_name,
                              const char *artist_name,
                              uint32_t *track_ids, int track_count);

/* Create an MTP artist object (format 0xB218).
 * The Zune requires these for proper artist display.
 * Returns artist handle or 0 on failure. */
/* Reuse a matching existing artist before creating one. Failed inventory
 * reads return 0 without creating; existing duplicate records are not deleted. */
uint32_t zune_forge_artist(ZuneDeviceHandle dev,
                                    const char *artist_name);

/* Link an album or track to an artist via ArtistId (0xDAB9). */
int zune_link_artist(ZuneDeviceHandle dev, uint32_t object_id,
                        uint32_t artist_handle);

/* Get existing album objects from device (for merge during sync). */
typedef struct {
    uint32_t album_id;
    char *name;
    char *artist;
    uint32_t *track_ids;
    uint32_t track_count;
} ZuneAlbumObject;

ZuneAlbumObject *zune_get_albums(ZuneDeviceHandle dev, int *count);
void zune_free_albums(ZuneAlbumObject *albums, int count);

/* ---- Album Art & Thumbnails ---- */

/* Set album art on a track/album object. JPEG data sent as
 * MTP representative sample. */
int zune_brand(ZuneDeviceHandle dev, uint32_t item_id,
                        const uint8_t *jpeg_data, size_t jpeg_len);

/* Get thumbnail/art from device. Tries representative sample first,
 * then MTP thumbnail. Does NOT download full file.
 * Writes to cache_path. Returns 0 on success. */
int zune_grab_thumb(ZuneDeviceHandle dev, uint32_t item_id,
                        const char *cache_path);

/* Get photo thumbnail with full fallback chain:
 * representative sample → MTP thumbnail → download + ffmpeg resize.
 * Use for photos only (small files). NOT for videos. */
int zune_grab_photo_thumb(ZuneDeviceHandle dev, uint32_t item_id,
                              const char *cache_path);

/* ---- Transcoding ---- */

/* Transcode audio to Zune-compatible MP3 (320kbps CBR, ID3v2.3).
 * Returns path to temp file (caller frees + unlinks). NULL on failure. */
char *zune_arm_audio(const char *input_path);

/* Retag an existing MP3 from ID3v2.4 to ID3v2.3 (stream copy, no re-encoding).
 * The Zune ignores ID3v2.4 tags entirely, showing "Unknown Artist/Album".
 * Returns path to temp file (caller frees + unlinks). NULL on failure. */
char *zune_retag(const char *mp3_path);

/* Transcode video for specific Zune model.
 * profile: 0=Zune30(WMV), 1=Zune80(H264), 2=ZuneHD, 3=ZuneHD720p
 * Returns path to temp file (caller frees + unlinks). NULL on failure. */
char *zune_arm_video(const char *input_path, ZuneModel model);

/* Arm a TV episode for the Zune with metadata embedded in container.
 * Same as zune_arm_video but also embeds series/season/episode. */
char *zune_arm_episode(const char *input_path, ZuneModel model,
                                 const char *series, int season, int episode);

/* ---- ZMDB (Zune Media Database) Fast Scan ---- */

/* DEPRECATED: Use zune_infiltrate() + zune_free_scan() instead.
 * ZuneLibrary only returns tracks/videos/photos.
 * ZuneDBLibrary returns everything: tracks, videos, photos,
 * playlists, albums, artists, photo albums. */
typedef struct {
    ZuneTrack *tracks;         int track_count;
    ZuneVideoFile *videos;     int video_count;
    ZunePhotoFile *photos;     int photo_count;
} ZuneLibrary;

/* DEPRECATED: Use zune_infiltrate() + zune_free_scan() instead. */
int zune_infiltrate_legacy(ZuneDeviceHandle dev, ZuneLibrary **out);
void zune_free_library(ZuneLibrary *lib);

/* ---- Sync Finalization ---- */

/* Signal device to rebuild its media database.
 * Call after bulk sync operations. Experimental — may not work on all models.
 * Returns 0 on success. */
int zune_finalize(ZuneDeviceHandle dev);

/* ---- Utility ---- */

/* Parse video filename for TV series metadata.
 * Detects S01E05, "Episode XX", trailing numbers, etc.
 * Returns 0 if no pattern detected (it's a movie). */
int zune_decode_filename(const char *filepath,
                               char **out_series, int *out_season,
                               int *out_episode);

/* Generate video thumbnail (ffmpeg frame grab at 10s).
 * Returns 0 on success. */
int zune_snap_thumb(const char *video_path, const char *output_path);

/* ---- Metadata Extraction ---- */

/* Metadata extracted by native libavformat, or ffprobe when unavailable.
 * Text is trimmed UTF-8; missing/blank tags are NULL. Existing member offsets
 * stay unchanged; consumers must rebuild when adding the trailing fields. */
typedef struct {
    char *title;
    char *artist;
    char *albumartist;   /* ID3 TPE2 / Vorbis albumartist — album-level grouping identifier */
    char *album;
    char *genre;
    uint16_t tracknumber;
    uint32_t duration_ms;
    int discnumber;      /* positive disc N from N or N/total; 0 if missing/invalid */
    int year;            /* positive four-digit date/year; 0 if missing/invalid */
} ZuneMetadata;

/* Inspect audio/video metadata. Nonblank container tags take precedence over
 * selected-stream tags (default audio, then first audio; video if no audio).
 * Returns 0 for successfully inspected media, -1 on failure with zeroed output.
 * Missing tags do not imply failure. Caller frees with zune_free_metadata();
 * free an earlier successful result before reusing the output struct. */
int zune_probe(const char *filepath, ZuneMetadata *out);
void zune_free_metadata(ZuneMetadata *meta);

/* ---- Progress Callbacks ---- */

/* Progress callback for file transfer operations. */
typedef void (*zune_progress_fn)(uint64_t bytes_sent, uint64_t bytes_total,
                                  void *userdata);

/* Send track with explicit metadata and progress callback. */
int zune_smuggle_track_ex(ZuneDeviceHandle dev, const char *filepath,
                        const char *title, const char *artist,
                        const char *album, const char *genre,
                        uint16_t tracknumber, uint32_t duration_ms,
                        zune_progress_fn progress, void *userdata,
                        uint32_t *out_item_id);

/* ---- ZuneDB Unified Library Scanner ---- */

/* Complete device library returned by zune_infiltrate().
 * All metadata extracted from the ZMDB binary database in a single
 * USB operation. No MTP enumeration fallbacks.
 * Art/thumbnails still fetched separately (binary MTP data). */

typedef struct {
    uint32_t album_id;    /* ZMDB atom_id */
    char *title;
    char *artist;
    uint16_t year;        /* HD only (from FILETIME), 0 on Classic */
} ZuneDBAlbum;

typedef struct {
    uint32_t artist_id;   /* ZMDB atom_id */
    char *name;
} ZuneDBArtist;

typedef struct {
    uint32_t album_id;    /* ZMDB atom_id for photo album */
    char *name;
} ZuneDBPhotoAlbum;

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

/* Infiltrate the Zune's internal media database via vendor opcode 0x1792.
 * Returns 0 on success, -1 on failure.
 * Single call replaces zune_infiltrate_legacy() + all MTP enumeration.
 * Caller frees with zune_free_scan(). */
int zune_infiltrate(ZuneDeviceHandle dev, ZuneDBLibrary **out);
void zune_free_scan(ZuneDBLibrary *lib);

/* ---- ZuneDB Research Tools ---- */

/* Dump raw ZMDB binary to a file for offline hex analysis.
 * Includes the 12-byte header + full payload. */
int zune_dump_raw(ZuneDeviceHandle dev, const char *output_path);

/* Scan all 96 ZMDB descriptors and log their contents to stderr.
 * Shows entry counts, schema types, and hex dumps of sample video records.
 * Use this to discover unknown fields and descriptor mappings. */
int zune_infiltrate_deep(ZuneDeviceHandle dev);

/* ---- Error Handling ---- */

/* Get last error message (thread-local). */
const char *zune_get_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ZUNE_H */
