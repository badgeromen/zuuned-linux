/*
 * libzune — Zune MTP Device Library
 *
 * A C library for Zune device management.
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

/* Marks the public interface. The library builds with -fvisibility=hidden,
 * so only these declarations are exported from the shared object. */
#if defined(__GNUC__) || defined(__clang__)
#  define ZUNE_API __attribute__((visibility("default")))
#else
#  define ZUNE_API
#endif

/* Marks a superseded function; the compiler's warning names its replacement. */
#if defined(__GNUC__) || defined(__clang__)
#  define ZUNE_DEPRECATED(msg) __attribute__((deprecated(msg)))
#else
#  define ZUNE_DEPRECATED(msg)
#endif

/* Status results. A function documented as returning a status returns
 * ZUNE_OK on success and ZUNE_BLOWN on failure; a few add documented codes
 * of their own (ZUNE_VIDEO_METADATA_INCOMPLETE, ZUNE_PODCAST_FAILED_CLEANED).
 * docs/API_REFERENCE.md lists every function's return convention. */
#define ZUNE_OK 0
#define ZUNE_BLOWN (-1)

/* ---- Zune HD artist catalog over USB ---- */

#define ZUNE_HD_ART_GALLERY_MAX 6
#define ZUNE_HD_ART_ASSET_MAX 8
#define ZUNE_HD_ART_API_VERSION 1u

typedef enum {
    ZUNE_HD_ART_OK = 0,             /* Every configured asset fully ACKed. */
    ZUNE_HD_ART_PARTIAL = 1,        /* Clean session; cached/no/partial requests. */
    ZUNE_HD_ART_UNSUPPORTED = 2,    /* Non-HD; no USB writes. */
    ZUNE_HD_ART_IDENTITY_CONFLICT = 3, /* Existing unbound/conflicting artist. */
    ZUNE_HD_ART_INVALID = -1,
    ZUNE_HD_ART_IO_ERROR = -2,
    ZUNE_HD_ART_CANCELLED = -3
} ZuneHDArtStatus;

typedef struct {
    const char *guid;              /* Canonical UUID; borrowed immutable UTF-8. */
    const uint8_t *jpeg;           /* Complete JPEG <=128 KiB, fully validated. */
    size_t size;
} ZuneHDArtImage;

typedef struct {
    uint32_t version;              /* ZUNE_HD_ART_API_VERSION */
    const char *artist_name;       /* Nonempty UTF-8, <=256 bytes. */
    const char *catalog_guid;      /* Persist it; never rotate to bypass cache. */
    ZuneHDArtImage background;     /* GUID = catalog GUID last nibble XOR 1. */
    ZuneHDArtImage thumbnail;      /* Separate unique ID. */
    ZuneHDArtImage gallery[ZUNE_HD_ART_GALLERY_MAX];
    size_t gallery_count;          /* 0..6 separate images. */
    const char *biography;         /* Optional plain UTF-8, not HTML. */
    size_t biography_size;         /* 1..4096 if supplied, otherwise zero. */
    unsigned duration_ms;         /* 1000..360000, network observation budget. */
} ZuneHDArtConfig;

typedef struct {
    char guid[37];
    unsigned requests;
    unsigned acknowledged;        /* Complete HTTP response ACKs, not display. */
} ZuneHDArtAssetResult;

typedef struct {
    ZuneHDArtStatus status;
    int ready_called;
    int network_closed;
    int transport_uncertain;      /* Stop device use, sever and reconnect. */
    int safe_to_continue;          /* False: no finalize, retry or other PTP. */
    unsigned http_requests;
    unsigned biography_requests;
    unsigned biography_acknowledged;
    size_t asset_count;            /* Background, thumbnail, then gallery. */
    ZuneHDArtAssetResult assets[ZUNE_HD_ART_ASSET_MAX];
} ZuneHDArtResult;

typedef struct {
    uint32_t handle;               /* Retained even if post-create proof fails. */
    int created;                  /* A create was accepted, not a full proof. */
    int write_attempted;           /* Never blindly retry an attempted create. */
    int transport_uncertain;
} ZuneHDArtistResult;

/* Device handle forward declaration used by the HD callbacks. */
struct ZuneDevice;
/* READY is called exactly once with PPP ready, on the serialized owner thread.
 * It may create/link artist/album/track objects synchronously on this device.
 * Return 0 on success, -1 on definite failure, -2 on uncertain transaction.
 * Never sever, finalize, nest serve, or launch another USB owner in callbacks.
 * Callback transactions have their own timeouts; duration bounds observation,
 * not arbitrary callback execution or full initialization. */
typedef int (*ZuneHDArtReady)(struct ZuneDevice *, void *context);
/* Progress is informational, synchronous and must not call device operations. */
typedef void (*ZuneHDArtProgress)(const ZuneHDArtResult *, void *context);

/* Software-only full validation. Returns OK or INVALID, sets thread-local error.
 * No files, host sockets or device operations. All buffers remain caller-owned. */
ZUNE_API ZuneHDArtStatus zune_hd_art_validate(const ZuneHDArtConfig *config);

/* Blocking native USB catalog session. Requires authenticated HD and exclusive
 * device ownership through return. Inputs must remain immutable until return.
 * Includes reference initialization and orderly tunnel closure when safe.
 * zune_abort cooperatively cancels; result reports whether reconnect is needed.
 * Does not finalize, delete, overwrite GUIDs, fetch providers, or own the device.
 * Requires a READY callback to prepare OR validate the artist binding using
 * zune_hd_art_ensure_artist; the callback runs before catalog traffic is served.
 * Existing device cache can cause PARTIAL with no requests; this is not success.
 * Physical display/persistence are separate from response acknowledgments. */
ZUNE_API ZuneHDArtStatus zune_hd_art_serve(struct ZuneDevice *device,
    const ZuneHDArtConfig *config, ZuneHDArtReady ready, void *ready_context,
    ZuneHDArtProgress progress, void *progress_context, ZuneHDArtResult *result);

/* Exact catalog binding: reuse one matching artist with the same GUID, or
 * create a fresh, fully tagged artist atomically when absence is established.
 * ASCII case/outer-space name matching matches ordinary forge behavior. Any
 * ambiguous same-name artist, GUID collision, existing zero/different GUID
 * yields IDENTITY_CONFLICT without writes. No retrofit, deletion or retry.
 * Call only on the serialized connection. On IO_ERROR inspect write_attempted,
 * handle and transport_uncertain before deciding whether normal sync can resume. */
ZUNE_API ZuneHDArtStatus zune_hd_art_ensure_artist(struct ZuneDevice *device,
    const char *artist_name, const char *catalog_guid, ZuneHDArtistResult *result);

/* ---- Version ---- */

#define LIBZUNE_VERSION_MAJOR 0
#define LIBZUNE_VERSION_MINOR 3
#define LIBZUNE_VERSION_PATCH 0

/* Return the library version string, "MAJOR.MINOR.PATCH", of the running
 * library (the macros above describe the header you compiled against). */
ZUNE_API const char *zune_dossier(void);

/* ---- Logging ---- */

/* Severity of a library log message, lowest first. */
typedef enum {
    ZUNE_CHATTER_WHISPER = 0,  /* debug: protocol detail, per-item progress */
    ZUNE_CHATTER_TALK    = 1,  /* info: lifecycle (connected, sent, created) */
    ZUNE_CHATTER_SHOUT   = 2,  /* warn: retries, fallbacks, partial results */
    ZUNE_CHATTER_SCREAM  = 3,  /* error: an operation or check failed */
    ZUNE_CHATTER_SILENT  = 4   /* threshold only: suppress everything */
} ZuneChatter;

/* Log callback. tag is the subsystem ("libzune", "ptp", "mtp", ...) and
 * message has no tag or trailing newline; both are valid only during the
 * call. It runs on the logging thread, possibly on several threads at once. */
typedef void (*ZuneWiretapFn)(ZuneChatter level, const char *tag,
                              const char *message, void *ctx);

/* Set the log callback. NULL restores the default stderr sink, which prints
 * "[tag] message". Process-wide; set it before opening a device. A message
 * already being delivered when the tap changes still reaches the old callback,
 * so keep its ctx valid until every device is closed. */
ZUNE_API void zune_wiretap(ZuneWiretapFn fn, void *ctx);

/* Set the minimum level that reaches the sink. Default: TALK, or the
 * ZUNE_CHATTER environment variable (whisper|talk|shout|scream|silent).
 * Process-wide; set it before opening a device. */
ZUNE_API void zune_set_chatter(ZuneChatter level);

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

/* Open USB and a PTP session, then attempt MTPZ authentication if data exists.
 * Synchronous. Returns NULL on connection failure; authentication failure or
 * missing data can still yield a handle. Does not scan/cache the library. */
ZUNE_API ZuneDeviceHandle zune_breach(void);

/* Close the connection and free the device. Accepts NULL.
 * Caller MUST finish/join every operation using this handle first. The internal
 * disconnecting flag and delay do not synchronize worker teardown. */
ZUNE_API void zune_sever(ZuneDeviceHandle dev);

/* Wipe and free the MTPZ keys loaded by zune_breach.
 * Process-wide teardown, e.g. at exit: call only when no device is open (after
 * the last zune_sever). Safe when nothing is loaded; a later zune_breach loads
 * the keys again. */
ZUNE_API void zune_go_dark(void);

/* Cached device info getters. Strings are borrowed until disconnect.
 * Battery is a connect-time snapshot; storage can be refreshed below. */
ZUNE_API const char *zune_get_name(ZuneDeviceHandle dev);
ZUNE_API const char *zune_get_model(ZuneDeviceHandle dev);
ZUNE_API const char *zune_get_serial(ZuneDeviceHandle dev);
ZUNE_API uint8_t     zune_get_battery(ZuneDeviceHandle dev);
ZUNE_API uint64_t    zune_get_capacity(ZuneDeviceHandle dev);
ZUNE_API uint64_t    zune_get_headroom(ZuneDeviceHandle dev);

/* Re-read live free/total space from the device (GetStorageInfo). The
 * get_capacity/get_headroom values are a connect-time snapshot; call this
 * before committing each file during a sync to avoid overcommitting the
 * device (StoreFull). Returns 0 on success, -1 on failure. */
ZUNE_API int         zune_refresh_storage(ZuneDeviceHandle dev);
ZUNE_API ZuneModel   zune_identify(ZuneDeviceHandle dev);

/* Device family from MTP property 0xD21A. Distinguishes HDD (Keel/Draco)
 * from flash (Scorpius) and HD (Pavo). Returns ZUNE_FAMILY_UNKNOWN if
 * the property is unavailable. */
ZUNE_API ZuneDeviceFamily zune_get_family(ZuneDeviceHandle dev);

/* Returns 1 if the device is HDD-based (Keel or Draco), 0 otherwise.
 * Useful for pacing decisions — HDD Zunes need slower I/O cadence. */
ZUNE_API int zune_is_hdd(ZuneDeviceHandle dev);

/* Set device friendly name. */
ZUNE_API int zune_rename(ZuneDeviceHandle dev, const char *new_name);

/* Check local device/backend/disconnecting state (1/0). No USB probe is made;
 * success does not establish that the physical device is responsive. */
ZUNE_API int zune_is_live(ZuneDeviceHandle dev);

/* Request cooperative cancellation of an in-progress transfer. Wait for its
 * return before teardown or clearing the flag; partial objects can remain. */
ZUNE_API void zune_abort(ZuneDeviceHandle dev);

/* Check if cancellation was requested. */
ZUNE_API int zune_is_aborted(ZuneDeviceHandle dev);

/* Clear cancellation flag (call before starting a new transfer). */
ZUNE_API void zune_clear_abort(ZuneDeviceHandle dev);

/* Cause of death for the last operation: the device's PTP response code
 * (0x2001 = OK), 0x02FF for transport-level failures (stall/timeout/short
 * read), or 0 if no response was read. Examine after any failed call to
 * learn WHY it died — 0x200C means the device is full, 0x2005 means the
 * device refused the operation, 0x02FF means the USB pipe is wounded. */
ZUNE_API uint16_t zune_autopsy(ZuneDeviceHandle dev);

/* Human-readable name for an autopsy code ("StoreFull", "AccessDenied"…). */
ZUNE_API const char *zune_autopsy_name(uint16_t code);

/* Clear USB stall on both endpoints. Call after PIPE/STALL/timeout errors
 * before retrying a transfer. Without this, a single stall causes the
 * entire sync to hang forever on macOS. */
ZUNE_API int zune_unjam(ZuneDeviceHandle dev);

/* Create a folder on the device. Returns folder ID or 0 on failure.
 * Uses the device's primary storage. */
ZUNE_API uint32_t zune_forge_folder(ZuneDeviceHandle dev, const char *name,
                             uint32_t parent_id);

/* Get folder name mapping (folder_id → name). Caller frees entries and array.
 * Returns number of entries, or -1 on failure, including a device that stops
 * answering partway through. A folder whose details cannot be read is left
 * out. */
typedef struct { uint32_t id; char *name; } ZuneFolderEntry;
ZUNE_API int zune_get_folders(ZuneDeviceHandle dev, ZuneFolderEntry **out);
ZUNE_API void zune_free_folders(ZuneFolderEntry *entries, int count);

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
ZUNE_API ZuneTrack *zune_get_tracks(ZuneDeviceHandle dev, int *count);
ZUNE_API void zune_free_tracks(ZuneTrack *tracks, int count);

/* Send an audio file as-is. File tags are NOT read: the title is the
 * filename without extension and the artist "Unknown Artist"; use
 * zune_smuggle_track_tagged to set metadata.
 * out_item_id: receives MTP item ID (for album art, etc.) */
ZUNE_API int zune_smuggle_track(ZuneDeviceHandle dev, const char *filepath,
                    uint32_t *out_item_id);

/* Send audio file with explicit metadata (overrides file tags). */
ZUNE_API int zune_smuggle_track_tagged(ZuneDeviceHandle dev, const char *filepath,
                               const char *title, const char *artist,
                               const char *album, const char *genre,
                               uint16_t tracknumber, uint32_t duration_ms,
                               uint32_t *out_item_id);

/* Delete track from device. */
ZUNE_API int zune_purge_track(ZuneDeviceHandle dev, uint32_t item_id);

/* Download track from device to local file. */
ZUNE_API int zune_extract_track(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);

/* Read track user state via MTP. Any output pointer may be NULL to skip.
 * Rating encoding: 0=neutral, 8=liked, 3=disliked.
 * Returns 0 if any requested playcount/rating read succeeds, otherwise -1.
 * Failed reads become zero. Skip count is always zero in this MTP path; use
 * ZMDB scan data when available. Success can therefore contain partial data. */
ZUNE_API int zune_get_track_state(ZuneDeviceHandle dev, uint32_t item_id,
                          uint16_t *out_playcount, uint8_t *out_rating,
                          uint32_t *out_skip_count);

/* Write track playcount and rating to device via MTP.
 * Rating encoding: 0=neutral, 8=liked, 3=disliked.
 * Returns 0 on success, -1 on failure. */
ZUNE_API int zune_set_track_state(ZuneDeviceHandle dev, uint32_t item_id,
                          uint16_t playcount, uint8_t rating);

/* Verify a track after upload by reading back properties from the device.
 * Checks title, artist, and filesize match expected values.
 * Returns 0 if all checks pass, -1 if any mismatch or read failure.
 * Pass NULL for expected_title/expected_artist to skip that check.
 * Pass 0 for expected_size to skip size check. */
ZUNE_API int zune_verify(ZuneDeviceHandle dev, uint32_t item_id,
                 const char *expected_title, const char *expected_artist,
                 uint64_t expected_size);

/* ---- Rename ----
 * Set the display Name (0xDC44) of any item — track, video, album,
 * playlist. The Zune UI lists show this string everywhere. */
ZUNE_API int zune_rename_item(ZuneDeviceHandle dev, uint32_t item_id, const char *new_name);

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
ZUNE_API int zune_sync_notify(ZuneDeviceHandle dev, const char *name, uint32_t op_kind,
                     uint32_t item_index, uint32_t total_items,
                     uint32_t progress_before, uint32_t progress_after);

/* ---- Diagnostics ----
 * Existence + size probe (GetObjectInfo) for auditing ZMDB rows against
 * real objects. Returns 0 if the object exists; -1 if the device can't
 * produce it (stranded DB entry). out_size/out_format may be NULL. */
ZUNE_API int zune_probe_object(ZuneDeviceHandle dev, uint32_t item_id,
                      uint64_t *out_size, uint16_t *out_format);
/* As above, plus the object's FILENAME (caller frees; may be NULL).
 * Backfills photo names the Classic ZMDB parse can't resolve. */
ZUNE_API int zune_probe_object_named(ZuneDeviceHandle dev, uint32_t item_id,
                            uint64_t *out_size, uint16_t *out_format,
                            char **out_name);

/* Raw GetObjectReferences (0x9810) — playlist/album member lists.
 * Caller frees *out_refs. Returns 0 on success. */
ZUNE_API int zune_get_item_refs(ZuneDeviceHandle dev, uint32_t item_id,
                       uint32_t **out_refs, int *out_count);

/* ---- Duplicate Detection ---- */

/* Search cached ZMDB library for an existing track by title+artist+album.
 * Requires a prior zune_infiltrate() call. Case-insensitive matching.
 * Returns first item_id if found, 0 if not found or no cached library.
 * No disc/track/content matching. The scan is borrowed: keep it allocated for
 * every find call; uploads do not automatically update the cached snapshot. */
ZUNE_API uint32_t zune_find_track(ZuneDeviceHandle dev,
                          const char *title, const char *artist,
                          const char *album);

/* Search borrowed cached ZMDB library by its legacy filename field, which
 * can be a display title rather than ObjectFileName. Keep scan allocated.
 * Returns first item_id if found, 0 if not found. */
ZUNE_API uint32_t zune_find_video(ZuneDeviceHandle dev, const char *filename);

/* Search cached ZMDB library for an existing photo by filename.
 * Returns item_id if found, 0 if not found. */
ZUNE_API uint32_t zune_find_photo(ZuneDeviceHandle dev, const char *filename);

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
ZUNE_API ZuneVideoFile *zune_get_videos(ZuneDeviceHandle dev, int *count);
ZUNE_API void zune_free_videos(ZuneVideoFile *videos, int count);

/* A complete media upload whose requested metadata/art was not all saved.
 * The object exists: do NOT retry the upload. Inspect zune_get_error() and
 * use the returned item ID for targeted repair. */
enum { ZUNE_VIDEO_METADATA_INCOMPLETE = 1 };

/* Send any video category with an independent transport filename and title.
 * object_filename includes its playable extension; title is the exact Name
 * (0xDC44), with no added series, episode numbering, or extension.
 * TV series/season/episode are separate vendor properties. For TV genre only,
 * nonempty series writes all three, including zero. Numbers must be >= 0.
 * Returns 0 for complete success, ZUNE_VIDEO_METADATA_INCOMPLETE when the
 * media exists but metadata/art failed, or -1 for invalid input/file failure.
 * *out_item_id is nonzero for both completed-upload results, otherwise zero.
 * The caller must provide out_item_id. All returned video strings are owned
 * by their containing result and released by the matching free function.
 * Struct fields were appended: rebuild consumers against this header. */
ZUNE_API int zune_smuggle_video_named(ZuneDeviceHandle dev, const char *filepath,
                            const char *object_filename, const char *title,
                            uint16_t meta_genre,
                            const char *series, int season, int episode,
                            const char *description,
                            const uint8_t *poster_jpeg, size_t poster_len,
                            uint32_t *out_item_id);

/* Deprecated: the four legacy category helpers below return upload success
 * even if later metadata or art fails. Use zune_smuggle_video_named for
 * distinct title/filename and partial-metadata status. Send a movie with
 * MetaGenre=0x25 (Movie).
 * description: TMDB/user description (UCS-2 encoded internally).
 * poster_jpeg/poster_len: poster art sent as representative sample.
 * Pass NULL/0 for no poster. */
ZUNE_API int zune_smuggle_movie(ZuneDeviceHandle dev, const char *filepath,
                     const char *display_name, const char *description,
                     const uint8_t *poster_jpeg, size_t poster_len,
                     uint32_t *out_item_id) ZUNE_DEPRECATED("use zune_smuggle_video_named");

/* Read series/season/episode vendor properties from a TV video on device.
 * out_title is Name (0xDC44), falling back to ObjectFileName if unavailable.
 * out_series/out_title are allocated — caller frees. Returns 0 if series found. */
ZUNE_API int zune_get_series_info(ZuneDeviceHandle dev, uint32_t item_id,
                                char **out_series, int *out_season, int *out_episode,
                                char **out_title);

/* Send a TV episode. Sets MetaGenre=0x26 (TV Show) + vendor props.
 * series/season/episode written to Zune vendor properties
 * (0xDA9A, 0xDAB5, 0xDAB6). */
ZUNE_API int zune_smuggle_episode(ZuneDeviceHandle dev, const char *filepath,
                          const char *display_name,
                          const char *series, int season, int episode,
                          const char *description,
                          const uint8_t *poster_jpeg, size_t poster_len,
                          uint32_t *out_item_id) ZUNE_DEPRECATED("use zune_smuggle_video_named");

/* Send a music video. Sets MetaGenre=0x23 (Music Video). */
ZUNE_API int zune_smuggle_clip(ZuneDeviceHandle dev, const char *filepath,
                           const char *display_name,
                           const uint8_t *poster_jpeg, size_t poster_len,
                           uint32_t *out_item_id) ZUNE_DEPRECATED("use zune_smuggle_video_named");

/* Send an "Other" video. Sets MetaGenre=0x21 (Other). */
ZUNE_API int zune_smuggle_other(ZuneDeviceHandle dev, const char *filepath,
                       const char *display_name, const char *description,
                       const uint8_t *poster_jpeg, size_t poster_len,
                       uint32_t *out_item_id) ZUNE_DEPRECATED("use zune_smuggle_video_named");

/* Delete video from device. */
ZUNE_API int zune_purge_video(ZuneDeviceHandle dev, uint32_t item_id);

/* Download video from device to local file. */
ZUNE_API int zune_extract_video(ZuneDeviceHandle dev, uint32_t item_id,
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
ZUNE_API ZunePhotoFile *zune_get_photos(ZuneDeviceHandle dev, int *count);
ZUNE_API void zune_free_photos(ZunePhotoFile *photos, int count);

/* Get original image dimensions for a photo on device (MTP properties 0xDC87/0xDC88).
 * Returns 0 on success, -1 on failure. */
ZUNE_API int zune_get_dimensions(ZuneDeviceHandle dev, uint32_t item_id,
                               uint32_t *width, uint32_t *height);

/* Get photo albums (folders) on device. Caller frees. */
ZUNE_API ZunePhotoAlbum *zune_get_photo_albums(ZuneDeviceHandle dev,
                                       ZunePhotoFile *photos, int photo_count,
                                       int *album_count);
ZUNE_API void zune_free_photo_albums(ZunePhotoAlbum *albums, int count);

/* Arm a photo for Zune: resize to 480px max, convert to baseline JPEG.
 * Returns path to temp file (caller frees + unlinks). NULL on failure.
 * Called automatically by zune_smuggle_photo(), but exposed for preview/caching. */
ZUNE_API char *zune_arm_photo(const char *input_path);

/* Send photo to device, optionally into a named album (folder).
 * album_name=NULL sends to root. Creates album if it doesn't exist.
 * Automatically resizes and converts to JPEG for Zune compatibility. */
ZUNE_API int zune_smuggle_photo(ZuneDeviceHandle dev, const char *filepath,
                     const char *album_name);

/* Delete photo from device. */
ZUNE_API int zune_purge_photo(ZuneDeviceHandle dev, uint32_t item_id);

/* Download photo from device to local file. */
ZUNE_API int zune_extract_photo(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);

/* ---- Playlists ---- */

typedef struct {
    uint32_t playlist_id;
    char *name;
    uint32_t *track_ids;
    uint32_t track_count;
} ZunePlaylist;

/* Get all playlists. Caller frees. */
ZUNE_API ZunePlaylist *zune_get_playlists(ZuneDeviceHandle dev, int *count);
ZUNE_API void zune_free_playlists(ZunePlaylist *playlists, int count);

/* Create playlist with track IDs. Returns playlist_id or 0 on failure. */
ZUNE_API uint32_t zune_forge_playlist(ZuneDeviceHandle dev, const char *name,
                               uint32_t *track_ids, int count);

/* Update existing playlist's track list. */
ZUNE_API int zune_rewire_playlist(ZuneDeviceHandle dev, uint32_t playlist_id,
                          const char *name,
                          uint32_t *track_ids, int count);

/* Delete playlist. */
ZUNE_API int zune_purge_playlist(ZuneDeviceHandle dev, uint32_t playlist_id);

/* ---- MTP Album/Artist Object Hierarchy ---- */

/* The Zune reads album/artist metadata from MTP abstract objects,
 * not from individual track properties. After sending tracks, create
 * these objects and link them for proper display on the device.
 * Reference: zune-explorer's zune-manager.js _createAlbumObjects() */

/* Create an MTP album object and associate it with the given track IDs.
 * Returns album_id or 0 on failure. */
ZUNE_API uint32_t zune_forge_album(ZuneDeviceHandle dev,
                                   const char *album_name,
                                   const char *artist_name,
                                   const char *genre,
                                   uint32_t *track_ids, int track_count);

/* Set existing album metadata and replace references with the supplied nonempty
 * list. Caller performs any merge; empty input does not clear references.
 * Name/artist failures only log warnings; return 0 does not certify metadata. */
ZUNE_API int zune_rewire_album(ZuneDeviceHandle dev, uint32_t album_id,
                              const char *album_name,
                              const char *artist_name,
                              uint32_t *track_ids, int track_count);

/* Create an MTP artist object (format 0xB218).
 * The Zune requires these for proper artist display.
 * Returns artist handle or 0 on failure. */
/* Reuse a matching existing artist before creating one. Failed inventory
 * reads return 0 without creating; existing duplicate records are not deleted. */
ZUNE_API uint32_t zune_forge_artist(ZuneDeviceHandle dev,
                                    const char *artist_name);

/* Link an album or track to an artist via ArtistId (0xDAB9). */
ZUNE_API int zune_link_artist(ZuneDeviceHandle dev, uint32_t object_id,
                        uint32_t artist_handle);

/* Get existing album objects from device (for merge during sync). */
typedef struct {
    uint32_t album_id;
    char *name;
    char *artist;
    uint32_t *track_ids;
    uint32_t track_count;
} ZuneAlbumObject;

ZUNE_API ZuneAlbumObject *zune_get_albums(ZuneDeviceHandle dev, int *count);
/* Strict complete album inventory: 0 includes a proven empty list; -1 returns
 * NULL/zero with no partial rows. Every name, artist and reference list must be
 * readable and well formed. Use before identity-sensitive create/reuse decisions.
 * Caller owns the returned array and releases it with zune_free_albums. */
ZUNE_API int zune_get_albums_checked(struct ZuneDevice *device,
                           ZuneAlbumObject **albums, int *count);

ZUNE_API void zune_free_albums(ZuneAlbumObject *albums, int count);

/* ---- Podcasts (experimental; Keel audio/art confirmed; HD acceptance pending) ---- */

/* Mutation failed, but deletion of its newly created object was confirmed.
 * This is NOT success and does not authorize an automatic retry. Ordinary -1
 * remains unclassified: a zero output ID alone never proves cleanup. */
#define ZUNE_PODCAST_FAILED_CLEANED (-2)

typedef struct {
    uint32_t item_id;
    char *name;
    char *author;
    char *feed_url;
} ZunePodcastSeries;

/* Complete series inventory, including exact feed identity. 0 succeeds (NULL
 * array/count 0 is empty); -1 fails with no partial output. Caller frees with
 * zune_free_podcast_series. Requires a completed MTPZ handshake so an
 * unauthenticated inventory cannot be mistaken for an empty library.
 * Synchronous; serialize with all other device calls. */
ZUNE_API int zune_get_podcast_series(ZuneDeviceHandle dev, ZunePodcastSeries **series, int *count);
ZUNE_API void zune_free_podcast_series(ZunePodcastSeries *series, int count);

typedef struct {
    uint32_t item_id, parent_id, series_id, duration_ms;
    uint64_t filesize;
    char *title, *author, *series_name;
} ZunePodcastEpisode;

/* Read-only native schema-0x10 inventory, using wildcard handles (not a format
 * filter). Authenticated, active Keel/Pavo/Draco sessions only; serialize device I/O.
 * 0 success, -1 failure. Both outputs required; failure returns NULL/count 0,
 * never partial data. Empty success also returns NULL/count 0. Every string and
 * the array are owned; release with the matching free function. Group by
 * series_id, never title. series_name may be empty on the firmware; resolve it
 * from zune_get_podcast_series using series_id. No writes or playback claims. */
ZUNE_API int zune_get_podcast_episodes(ZuneDeviceHandle dev, ZunePodcastEpisode **episodes, int *count);
ZUNE_API void zune_free_podcast_episodes(ZunePodcastEpisode *episodes, int count);

typedef struct {
    uint32_t item_id, series_id, parent_id;
    char *feed_url, *source_url; /* Owned exact UTF-8 identities. */
} ZunePodcastEpisodeSource;

/* Complete exact source map for native podcast cleanup, read-only and serialized.
 * Requires authenticated active Keel/Pavo/Draco. Reads series once and each distinct
 * series folder's MP3 inventory once, independent of local subscription size.
 * 0 succeeds (NULL/count zero is known empty); -1 means invalid/unavailable with
 * no partial output. Duplicate feed/source identities or handles fail closed.
 * Both outputs required; free array/strings with the matching free function.
 * No title matching, writes, or implicit deletion authorization. */
ZUNE_API int zune_get_podcast_episode_sources(ZuneDeviceHandle dev, ZunePodcastEpisodeSource **sources, int *count);
ZUNE_API void zune_free_podcast_episode_sources(ZunePodcastEpisodeSource *sources, int count);

/* Independent persisted firmware listening state. marked_played is specifically
 * bit 0x200, not a nonzero flags word or a position/duration heuristic. A played
 * episode may retain a nonzero bookmark. This does not prove uninterrupted
 * natural completion; firmware/manual played actions are not distinguishable. */
typedef struct {
    uint32_t item_id, series_id, duration_ms, bookmark_ms, raw_flags;
    uint8_t marked_played;
    char *title; /* Owned validated UTF-8, for exact live-inventory correlation. */
} ZunePodcastState;

/* Fresh read-only complete ZMDB state, authenticated active Keel/Draco (v2) or Pavo (v5).
 * 0 success (NULL/count 0 is a known empty snapshot), -1 unavailable/unsupported/
 * malformed/partial with NULL/count 0. Both output pointers required. Unknown
 * state must never be interpreted as played. Caller owns the array/title strings and frees
 * with zune_free_podcast_states. Serialize with all other device operations.
 * This API neither deletes nor authorizes deletion. Join exact item/series IDs
 * to a fresh live identity inventory, including exact title and duration, before
 * any separate caller policy acts. */
ZUNE_API int zune_get_podcast_states(ZuneDeviceHandle dev, ZunePodcastState **states, int *count);
ZUNE_API void zune_free_podcast_states(ZunePodcastState *states, int count);

/* Reuse exactly one live feed URL match, otherwise create a zero-byte series
 * in the explicit parent folder with a safe .ser filename. Never merges by
 * title or overwrites an existing match's metadata. Required strings: filename,
 * name, feed_url; author may be empty. 0 success, negative failure.
 * ZUNE_PODCAST_FAILED_CLEANED proves the new object was removed; otherwise -1.
 * out_id is required:
 * on failure it is nonzero ONLY if a newly created orphan could not be removed.
 * Do not blindly retry that failure. No artwork/finalize is implicit.
 * Requires a completed MTPZ handshake; a non-NULL handle alone is insufficient. */
ZUNE_API int zune_forge_podcast_series(ZuneDeviceHandle dev, uint32_t parent_id,
                             const char *filename, const char *name,
                             const char *author, const char *feed_url,
                             uint32_t *out_id);

typedef struct {
    const char *filename;      /* Requested safe .mp3 basename; firmware may normalize it */
    const char *title;
    const char *author;
    const char *series_name;
    const char *source_url;    /* Nonempty stable enclosure URL */
    const char *description;   /* UTF-8; NULL means empty */
    const char *date_authored; /* Optional YYYYMMDDTHHMMSS or that plus Z */
    uint32_t series_id;
    uint32_t duration_ms;
} ZunePodcastEpisodeInput;

/* Upload ONE caller-prepared MP3 audio episode (no transcoding). Reuses a live
 * matching series-ID + source-URL object only if title and size match; ambiguous
 * or incompatible existing matches fail without writes. The caller maintains
 * feed/GUID identity across enclosure-URL changes. Validates series identity,
 * descriptor types and property/size readback; no implicit finalization.
 * 0 success, negative failure, including ZUNE_PODCAST_FAILED_CLEANED.
 * out_id is required, with the same orphan contract as forge_podcast_series. A failed
 * payload removes the new episode and retries (up to three attempts); an unanswered
 * creation is never retried. ZUNE_PODCAST_FAILED_CLEANED means every episode this
 * call created was removed; a nonzero out_id on failure could not be. This does
 * not establish device playback/played-state/bookmark support. Requires a
 * completed MTPZ handshake. Keel/Pavo/Draco schema 0x10 establishes podcast classification
 * when MetaGenre reads zero; its redundant SeriesName may read empty. */
ZUNE_API int zune_smuggle_podcast_episode(ZuneDeviceHandle dev, uint32_t parent_id,
                                 const char *filepath,
                                 const ZunePodcastEpisodeInput *episode,
                                 uint32_t *out_id);

/* Read-only podcast lookup for queue admission and retry. Exact feed + source
 * URL, never title. Outputs are zeroed on failure; absent series/episode is
 * success with zero IDs. A matched series supplies its actual parent folder.
 * Ambiguous, unreadable or wrongly classified objects fail closed. */
ZUNE_API int zune_find_podcast_episode(ZuneDeviceHandle dev, const char *feed_url,
                              const char *source_url, uint32_t *parent_id,
                              uint32_t *series_id, uint32_t *episode_id);

/* ---- Audiobooks (experimental; Keel/Pavo/Draco multipart MP3 gates passed) ---- */
typedef struct {
    uint32_t item_id, parent_id, part_number, duration_ms;
    uint64_t filesize;
    char *title, *author, *book_title;
} ZuneAudiobookPart;

/* Read-only native schema-0x12 inventory. Same ownership, authentication,
 * wildcard enumeration, all-or-nothing and serialization contracts as
 * zune_get_podcast_episodes. Group by parent_id, never book title; returned
 * order is device inventory order, so sort by part_number in the caller. */
ZUNE_API int zune_get_audiobook_parts(ZuneDeviceHandle dev, ZuneAudiobookPart **parts, int *count);
ZUNE_API void zune_free_audiobook_parts(ZuneAudiobookPart *parts, int count);

typedef struct {
    const char *book_key;   /* 64 lowercase hex SHA-256 of caller-owned book identity */
    const char *book_title;
    const char *author;     /* Empty permitted; NULL rejected */
    const char *title;
    uint16_t part_number;   /* Stable, positive reviewed order */
    uint32_t duration_ms;
} ZuneAudiobookPartInput;
/* Live read-only identity lookup; no ZMDB cache needed. 0 complete, -1 failure.
 * Outputs required and zeroed on failure. Refuses conflicting same-title books
 * in other folders rather than allowing firmware to merge editions/authors. */
ZUNE_API int zune_find_audiobook_part(ZuneDeviceHandle dev, const ZuneAudiobookPartInput *part,
                            uint32_t *folder_id, uint32_t *part_id);
/* Caller-prepared immutable MP3, no transcoding/chapters/artwork/finalize.
 * 0 success, -1 failure. Reuses exact live identity plus metadata/size.
 * A failed payload removes the new part and retries (up to three attempts); an
 * unanswered creation is never retried. Nonzero out_id on failure (a part that could
 * not be removed, or one that failed readback after upload) must be retained for
 * manual inspection. Transport loss can leave uncertain state even with ID zero.
 * Requires successful authentication and serialized ownership. Keel/Pavo/Draco only. */
ZUNE_API int zune_smuggle_audiobook_part(ZuneDeviceHandle dev, const char *filepath,
                               const ZuneAudiobookPartInput *part, uint32_t *out_id);

/* ---- Album Art & Thumbnails ---- */

/* Set album art on a track/album object. JPEG data sent as
 * MTP representative sample. */
ZUNE_API int zune_brand(ZuneDeviceHandle dev, uint32_t item_id,
                        const uint8_t *jpeg_data, size_t jpeg_len);

/* Read the largest artwork the device accepts for an item.
 * The device declares a maximum width and height in pixels and a maximum
 * JPEG size in bytes for each object format. 0 means it declares no limit.
 * Read-only; each format's answer is cached until sever. Firmware may keep
 * larger images but not show them (Zune 30: 240x240, 1 MiB).
 * Returns 0 on success, -1 on failure (all outputs 0). */
ZUNE_API int zune_get_brand_limits(ZuneDeviceHandle dev, uint32_t item_id,
                                   uint32_t *max_width, uint32_t *max_height,
                                   uint32_t *max_bytes);

/* Get thumbnail/art from device. Tries representative sample first,
 * then MTP thumbnail. Does NOT download full file.
 * Writes to cache_path. Returns 0 on success. */
ZUNE_API int zune_grab_thumb(ZuneDeviceHandle dev, uint32_t item_id,
                        const char *cache_path);

/* Get photo thumbnail with full fallback chain:
 * representative sample → MTP thumbnail → download + ffmpeg resize.
 * Use for photos only (small files). NOT for videos. */
ZUNE_API int zune_grab_photo_thumb(ZuneDeviceHandle dev, uint32_t item_id,
                              const char *cache_path);

/* ---- Transcoding ---- */

/* Legacy audio/video transcode and retag helpers use a hard-coded Homebrew
 * ffmpeg executable path; see docs/API_REFERENCE.md for portability limits.
 * Transcode audio to MP3 (320kbps CBR, ID3v2.3), stripping source metadata.
 * Returns path to temp file (caller frees + unlinks). NULL on failure. */
ZUNE_API char *zune_arm_audio(const char *input_path);

/* Retag an existing MP3 from ID3v2.4 to ID3v2.3 (stream copy, no re-encoding).
 * The Zune ignores ID3v2.4 tags entirely, showing "Unknown Artist/Album".
 * Returns path to temp file (caller frees + unlinks). NULL on failure. */
ZUNE_API char *zune_retag(const char *mp3_path);

/* Transcode video for specific Zune model.
 * profile: 0=Zune30(WMV), 1=Zune80(H264), 2=ZuneHD, 3=ZuneHD720p
 * Returns path to temp file (caller frees + unlinks). NULL on failure. */
ZUNE_API char *zune_arm_video(const char *input_path, ZuneModel model);

/* Arm a TV episode for the Zune with metadata embedded in container.
 * Same as zune_arm_video but also embeds series/season/episode. */
ZUNE_API char *zune_arm_episode(const char *input_path, ZuneModel model,
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

/* Deprecated: use zune_infiltrate() + zune_free_scan() instead. */
ZUNE_API int zune_infiltrate_legacy(ZuneDeviceHandle dev, ZuneLibrary **out)
    ZUNE_DEPRECATED("use zune_infiltrate and zune_free_scan");
ZUNE_API void zune_free_library(ZuneLibrary *lib);

/* ---- Sync Finalization ---- */

/* Request device re-indexing after bulk writes. Model-dependent, best-effort.
 * Returns -1 for NULL, otherwise 0 even if individual vendor requests fail.
 * Inspect logs and verify persistence after re-index; 0 is not that proof. */
ZUNE_API int zune_finalize(ZuneDeviceHandle dev);

/* ---- Utility ---- */

/* Parse video filename for TV series metadata.
 * Detects S01E05, "Episode XX", trailing numbers, etc.
 * Returns 0 if no pattern detected (it's a movie). */
ZUNE_API int zune_decode_filename(const char *filepath,
                               char **out_series, int *out_season,
                               int *out_episode);

/* Generate video thumbnail (ffmpeg frame grab at 10s, or at the start of a
 * shorter clip). Input/output aliases are refused without modifying either.
 * Otherwise an existing output_path is replaced, and removed if the grab
 * fails. Returns 0 on success. */
ZUNE_API int zune_snap_thumb(const char *video_path, const char *output_path);

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
ZUNE_API int zune_probe(const char *filepath, ZuneMetadata *out);
ZUNE_API void zune_free_metadata(ZuneMetadata *meta);

/* ---- Progress Callbacks ---- */

/* Progress callback for file transfer operations. */
typedef void (*zune_progress_fn)(uint64_t bytes_sent, uint64_t bytes_total,
                                  void *userdata);

/* Send track with explicit metadata and progress callback. */
ZUNE_API int zune_smuggle_track_ex(ZuneDeviceHandle dev, const char *filepath,
                        const char *title, const char *artist,
                        const char *album, const char *genre,
                        uint16_t tracknumber, uint32_t duration_ms,
                        zune_progress_fn progress, void *userdata,
                        uint32_t *out_item_id);

/* ---- ZuneDB Unified Library Scanner ---- */

/* Library snapshot returned by zune_infiltrate(). ZMDB metadata availability
 * varies by model; some sizes/state fields remain unknown. An internal MTP
 * playlist fallback runs if no playlists were decoded. Art is fetched separately. */

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
 * May perform additional MTP playlist enumeration. On success the device borrows
 * the returned scan for find helpers; caller owns it and frees with free_scan.
 * Keep it allocated for find calls. Failed scans do not replace the cache. */
ZUNE_API int zune_infiltrate(ZuneDeviceHandle dev, ZuneDBLibrary **out);
/* Free scan and nested allocations. Does NOT clear the device's borrowed cache.
 * Do not use find helpers afterward until another successful scan replaces it. */
ZUNE_API void zune_free_scan(ZuneDBLibrary *lib);

/* ---- ZuneDB Research Tools ---- */

/* Dump raw ZMDB binary to a file for offline hex analysis.
 * Includes the 12-byte header + full payload. */
ZUNE_API int zune_dump_raw(ZuneDeviceHandle dev, const char *output_path);

/* Scan all 96 ZMDB descriptors and log their contents to stderr.
 * Shows entry counts, schema types, and hex dumps of sample video records.
 * Use this to discover unknown fields and descriptor mappings. */
ZUNE_API int zune_infiltrate_deep(ZuneDeviceHandle dev);

/* ---- Error Handling ---- */

/* Borrowed thread-local error message, or NULL. Every call that can fail clears
 * it on entry, so it describes the most recent such call on this thread; NULL
 * means that call recorded no message. Calls that cannot fail (zune_autopsy,
 * zune_autopsy_name, the free_* releasers, cached device getters, abort, sever
 * and the log settings) leave it unchanged, so they can be used while
 * reporting a failure. Some failure paths only log. */
ZUNE_API const char *zune_get_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ZUNE_H */
