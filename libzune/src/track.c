/*
 * track.c — Music track operations (list, send, delete, download)
 *
 * Part of libzune. Pure C, no GTK/GLib dependencies.
 */

#include "zune_internal.h"

/*
 * Helper: read a uint32 from an MTP object property.
 * Returns the value, or default_val on failure.
 */
static uint32_t read_u32_prop(ptp_session_t *ptp, uint32_t handle,
                               uint16_t prop, uint32_t default_val) {
    uint8_t *data = NULL;
    uint32_t len = 0;
    if (mtp_get_object_prop_value(ptp, handle, prop, &data, &len) == 0
        && data && len >= 4) {
        uint32_t val = zune_le32(data);
        free(data);
        return val;
    }
    free(data);
    return default_val;
}

/*
 * Helper: read a uint16 from an MTP object property.
 * Returns the value, or default_val on failure.
 */
static uint16_t read_u16_prop(ptp_session_t *ptp, uint32_t handle,
                               uint16_t prop, uint16_t default_val) {
    uint8_t *data = NULL;
    uint32_t len = 0;
    if (mtp_get_object_prop_value(ptp, handle, prop, &data, &len) == 0
        && data && len >= 2) {
        uint16_t val = zune_le16(data);
        free(data);
        return val;
    }
    free(data);
    return default_val;
}

/* ---- Get all tracks ---- */

ZuneTrack *zune_get_tracks(ZuneDevice *dev, int *count) {
    zune_clear_error();
    if (!dev || !count) {
        zune_set_error("Invalid device or count pointer");
        if (count) *count = 0;
        return NULL;
    }

    *count = 0;

    if (ZUNE_FLAG_GET(dev->disconnecting)) {
        zune_set_error("Device is disconnecting");
        return NULL;
    }

    /* Get handles for MP3 and WMA tracks. A failed query is an error, not
     * an empty library: callers use this list to decide what exists. */
    uint32_t *mp3_handles = NULL;
    int mp3_count = 0;
    uint32_t *wma_handles = NULL;
    int wma_count = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_MP3,
                               0x00000000, &mp3_handles, &mp3_count) < 0
        || mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_WMA,
                                  0x00000000, &wma_handles, &wma_count) < 0) {
        free(mp3_handles);
        free(wma_handles);
        zune_set_error("Could not list device tracks (%s 0x%04X)",
                       zune_autopsy_name(dev->ptp.last_response), dev->ptp.last_response);
        return NULL;
    }

    int n = mp3_count + wma_count;
    if (n == 0) {
        free(mp3_handles);
        free(wma_handles);
        return NULL;
    }

    ZuneTrack *tracks = calloc(n, sizeof(ZuneTrack));
    if (!tracks) {
        zune_set_error("Out of memory allocating %d tracks", n);
        free(mp3_handles);
        free(wma_handles);
        return NULL;
    }

    int idx = 0;

    /* Process MP3 handles */
    for (int i = 0; i < mp3_count && idx < n; i++) {
        if (ZUNE_FLAG_GET(dev->disconnecting)) break;

        uint32_t h = mp3_handles[i];
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, h, &info) != 0)
            continue;

        tracks[idx].item_id = h;
        tracks[idx].filesize = info.object_compressed_size;

        /* Read metadata properties */
        char *title = mtp_read_string_prop(&dev->ptp, h, MTP_OPC_Name);
        tracks[idx].title = title ? title : strdup(info.filename ? info.filename : "Unknown");

        char *artist = mtp_read_string_prop(&dev->ptp, h, MTP_OPC_Artist);
        tracks[idx].artist = artist ? artist : strdup("Unknown Artist");

        char *album = mtp_read_string_prop(&dev->ptp, h, MTP_OPC_AlbumName);
        tracks[idx].album = album ? album : strdup("Unknown Album");

        char *genre = mtp_read_string_prop(&dev->ptp, h, MTP_OPC_Genre);
        tracks[idx].genre = genre ? genre : strdup("Unknown");

        tracks[idx].duration_ms = read_u32_prop(&dev->ptp, h, MTP_OPC_Duration, 0);
        tracks[idx].tracknumber = read_u16_prop(&dev->ptp, h, MTP_OPC_Track, 0);

        mtp_free_object_info(&info);
        idx++;
    }

    /* Process WMA handles */
    for (int i = 0; i < wma_count && idx < n; i++) {
        if (ZUNE_FLAG_GET(dev->disconnecting)) break;

        uint32_t h = wma_handles[i];
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, h, &info) != 0)
            continue;

        tracks[idx].item_id = h;
        tracks[idx].filesize = info.object_compressed_size;

        char *title = mtp_read_string_prop(&dev->ptp, h, MTP_OPC_Name);
        tracks[idx].title = title ? title : strdup(info.filename ? info.filename : "Unknown");

        char *artist = mtp_read_string_prop(&dev->ptp, h, MTP_OPC_Artist);
        tracks[idx].artist = artist ? artist : strdup("Unknown Artist");

        char *album = mtp_read_string_prop(&dev->ptp, h, MTP_OPC_AlbumName);
        tracks[idx].album = album ? album : strdup("Unknown Album");

        char *genre = mtp_read_string_prop(&dev->ptp, h, MTP_OPC_Genre);
        tracks[idx].genre = genre ? genre : strdup("Unknown");

        tracks[idx].duration_ms = read_u32_prop(&dev->ptp, h, MTP_OPC_Duration, 0);
        tracks[idx].tracknumber = read_u16_prop(&dev->ptp, h, MTP_OPC_Track, 0);

        mtp_free_object_info(&info);
        idx++;
    }

    free(mp3_handles);
    free(wma_handles);

    *count = idx;
    return tracks;
}

/* ---- Free tracks ---- */

void zune_free_tracks(ZuneTrack *tracks, int count) {
    if (!tracks) return;
    for (int i = 0; i < count; i++) {
        free(tracks[i].title);
        free(tracks[i].artist);
        free(tracks[i].album);
        free(tracks[i].genre);
    }
    free(tracks);
}

/* ---- Send track (metadata from file tags via ffprobe) ---- */

/*
 * Helper: get file size using stat(). Returns 0 on failure.
 */
static uint64_t get_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return (uint64_t)st.st_size;
}

/*
 * Helper: determine MTP format code from file extension.
 */
static uint16_t format_from_ext(const char *filepath) {
    const char *dot = strrchr(filepath, '.');
    if (dot && (strcasecmp(dot, ".wma") == 0))
        return MTP_OFC_WMA;
    return MTP_OFC_MP3;
}

/*
 * Helper: extract the basename from a filepath.
 * Returns a pointer into the filepath string (no allocation).
 */
static const char *basename_of(const char *filepath) {
    const char *base = strrchr(filepath, '/');
    return base ? base + 1 : filepath;
}

/*
 * Internal: send a track file and set metadata properties.
 * Returns 0 on success, -1 on failure. Sets *out_item_id on success.
 */
static int send_track_internal(ZuneDevice *dev, const char *filepath,
                                const char *title, const char *artist,
                                const char *album, const char *genre,
                                uint16_t tracknumber, uint32_t duration_ms,
                                uint32_t *out_item_id) {
    /* AlbumName is read-only after creation on the Zune (0x200F); album
     * display comes from album objects, so the album is not sent here. */
    (void)album;
    uint64_t fsize = get_file_size(filepath);
    if (fsize == 0) {
        zune_set_error("Cannot stat file: %s", filepath);
        return -1;
    }
    /* MTP ObjectCompressedSize is 32-bit — a bigger file would silently
     * truncate modulo 2^32 and corrupt the transfer. Refuse instead. */
    if (fsize > 0xFFFFFFFFULL) {
        zune_set_error("File too large for MTP (%llu bytes, max 4 GiB): %s",
                       (unsigned long long)fsize, filepath);
        return -1;
    }

    uint16_t format = format_from_ext(filepath);
    const char *fname = basename_of(filepath);

    /* The object's ObjectInfo dataset; the engine creates and fills it. */
    mtp_object_info_t info;
    memset(&info, 0, sizeof(info));
    info.storage_id = dev->storage_id;
    info.object_format = format;
    info.object_compressed_size = (uint32_t)fsize;
    info.parent_object = 0;
    info.filename = strdup(fname);

    zune_upload_t up = { .kind = "track", .path = filepath, .size = (uint32_t)fsize,
                         .parent = 0, .info = &info };
    uint32_t item_id = 0;
    int status = zune_smuggle_core(dev, &up, &item_id);
    free(info.filename);
    if (status != ZUNE_UPLOAD_SENT)
        return -1;          /* error set and zune_autopsy kept by the engine */

    /* Then set metadata properties
     * Note: AlbumName (0xDC9A) and AlbumArtist (0xDC9B) are read-only
     * post-creation on the Zune (0x200F Access Denied). The Zune gets
     * album/artist display from album objects, not track properties.
     * Failures here are logged but don't fail the track — the file IS on
     * the device (matches the Windows client's log-and-continue posture). */
    int prop_fails = 0;
    if (title && title[0])
        prop_fails += mtp_set_object_prop_value_str(&dev->ptp, item_id, MTP_OPC_Name, title) != 0;
    if (artist && artist[0])
        prop_fails += mtp_set_object_prop_value_str(&dev->ptp, item_id, MTP_OPC_Artist, artist) != 0;
    if (genre && genre[0])
        prop_fails += mtp_set_object_prop_value_str(&dev->ptp, item_id, MTP_OPC_Genre, genre) != 0;
    if (tracknumber > 0)
        prop_fails += mtp_set_object_prop_value_u16(&dev->ptp, item_id, MTP_OPC_Track, tracknumber) != 0;
    if (duration_ms > 0)
        prop_fails += mtp_set_object_prop_value_u32(&dev->ptp, item_id, MTP_OPC_Duration, duration_ms) != 0;
    if (prop_fails > 0)
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "track %u: %d metadata propert%s failed to set",
                item_id, prop_fails, prop_fails == 1 ? "y" : "ies");

    if (out_item_id) *out_item_id = item_id;
    return 0;
}

int zune_smuggle_track(ZuneDevice *dev, const char *filepath,
                    uint32_t *out_item_id) {
    zune_clear_error();
    if (!dev || !filepath) {
        zune_set_error("Invalid device or filepath");
        return ZUNE_BLOWN;
    }

    char *fallback_title = zune_filename_no_ext(filepath);
    int ret = send_track_internal(dev, filepath,
                                   fallback_title ? fallback_title : "Unknown",
                                   "Unknown Artist", "Unknown Album", "",
                                   0, 0, out_item_id);
    free(fallback_title);
    return ret;
}

/* ---- Send track with explicit metadata ---- */

int zune_smuggle_track_tagged(ZuneDevice *dev, const char *filepath,
                               const char *title, const char *artist,
                               const char *album, const char *genre,
                               uint16_t tracknumber, uint32_t duration_ms,
                               uint32_t *out_item_id) {
    zune_clear_error();
    if (!dev || !filepath) {
        zune_set_error("Invalid device or filepath");
        return ZUNE_BLOWN;
    }

    return send_track_internal(dev, filepath,
                                title && title[0] ? title : "Unknown",
                                artist && artist[0] ? artist : "Unknown Artist",
                                album && album[0] ? album : "Unknown Album",
                                genre && genre[0] ? genre : "",
                                tracknumber, duration_ms, out_item_id);
}

/* ---- Send track with progress callback ---- */

int zune_smuggle_track_ex(ZuneDevice *dev, const char *filepath,
                        const char *title, const char *artist,
                        const char *album, const char *genre,
                        uint16_t tracknumber, uint32_t duration_ms,
                        zune_progress_fn progress, void *userdata,
                        uint32_t *out_item_id) {
    zune_clear_error();
    if (!dev || !filepath) {
        zune_set_error("Invalid device or filepath");
        return ZUNE_BLOWN;
    }

    /*
     * The upload engine streams the file in transport-sized chunks, but no
     * per-chunk progress callback reaches this level yet. Report 0% at the
     * start and 100% at the end so callers can still update their UI.
     */
    uint64_t fsize = get_file_size(filepath);
    if (progress && fsize > 0)
        progress(0, fsize, userdata);

    int ret = send_track_internal(dev, filepath,
                                   title && title[0] ? title : "Unknown",
                                   artist && artist[0] ? artist : "Unknown Artist",
                                   album && album[0] ? album : "Unknown Album",
                                   genre && genre[0] ? genre : "",
                                   tracknumber, duration_ms, out_item_id);

    if (ret == 0 && progress && fsize > 0)
        progress(fsize, fsize, userdata);

    return ret;
}

/* ---- Delete track ---- */

int zune_purge_track(ZuneDevice *dev, uint32_t item_id) {
    zune_clear_error();
    if (!dev) {
        zune_set_error("Invalid device");
        return ZUNE_BLOWN;
    }

    int ret = mtp_delete_object(&dev->ptp, item_id);
    if (ret != 0) {
        zune_set_error("Failed to delete track %u", item_id);
        return ZUNE_BLOWN;
    }
    return ZUNE_OK;
}

/* ---- Download track ---- */

int zune_extract_track(ZuneDevice *dev, uint32_t item_id,
                         const char *dest_path) {
    zune_clear_error();
    if (!dev || !dest_path) {
        zune_set_error("Invalid device or destination path");
        return ZUNE_BLOWN;
    }

    int ret = mtp_get_object_to_file(&dev->ptp, item_id, dest_path);
    if (ret != 0) {
        zune_set_error("Failed to download track %u to %s", item_id, dest_path);
        return ZUNE_BLOWN;
    }
    return ZUNE_OK;
}

/* ---- Track verification after upload ---- */

int zune_verify(ZuneDevice *dev, uint32_t item_id,
                 const char *expected_title, const char *expected_artist,
                 uint64_t expected_size) {
    zune_clear_error();
    if (!dev || item_id == 0) {
        zune_set_error("Invalid device or item_id for verification");
        return ZUNE_BLOWN;
    }

    int mismatches = 0;

    /* Verify filesize via GetObjectInfo */
    if (expected_size > 0) {
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, item_id, &info) == 0) {
            if ((uint64_t)info.object_compressed_size != expected_size) {
                ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "verify: size mismatch for %u: "
                        "expected %llu, got %u", item_id,
                        (unsigned long long)expected_size,
                        info.object_compressed_size);
                mismatches++;
            }
            mtp_free_object_info(&info);
        } else {
            ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "verify: GetObjectInfo failed for %u",
                    item_id);
            mismatches++;
        }
    }

    /* Verify title */
    if (expected_title) {
        char *actual = mtp_read_string_prop(&dev->ptp, item_id, MTP_OPC_Name);
        if (!actual || strcmp(actual, expected_title) != 0) {
            ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "verify: title mismatch for %u: "
                    "expected '%s', got '%s'", item_id,
                    expected_title, actual ? actual : "(null)");
            mismatches++;
        }
        free(actual);
    }

    /* Verify artist */
    if (expected_artist) {
        char *actual = mtp_read_string_prop(&dev->ptp, item_id, MTP_OPC_Artist);
        if (!actual || strcmp(actual, expected_artist) != 0) {
            ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "verify: artist mismatch for %u: "
                    "expected '%s', got '%s'", item_id,
                    expected_artist, actual ? actual : "(null)");
            mismatches++;
        }
        free(actual);
    }

    if (mismatches > 0) {
        zune_set_error("Verification failed: %d mismatches for item %u",
                       mismatches, item_id);
        return ZUNE_BLOWN;
    }

    return ZUNE_OK;
}

/* ---- Track user state (playcount / rating) ---- */

int zune_get_track_state(ZuneDevice *dev, uint32_t item_id,
                          uint16_t *out_playcount, uint8_t *out_rating,
                          uint32_t *out_skip_count) {
    zune_clear_error();
    if (!dev || item_id == 0) {
        zune_set_error("Invalid device or item_id");
        return ZUNE_BLOWN;
    }

    int ok = 0;

    if (out_playcount) {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (mtp_get_object_prop_value(&dev->ptp, item_id, MTP_OPC_UseCount,
                                       &data, &len) == 0 && data && len >= 4) {
            *out_playcount = zune_le16(data);
            ok = 1;
        } else {
            *out_playcount = 0;
        }
        free(data);
    }

    if (out_rating) {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (mtp_get_object_prop_value(&dev->ptp, item_id, MTP_OPC_Rating,
                                       &data, &len) == 0 && data && len >= 2) {
            *out_rating = data[0];
            ok = 1;
        } else {
            *out_rating = 0;
        }
        free(data);
    }

    /* Skip count is ZMDB-only — no standard MTP property for it */
    if (out_skip_count) *out_skip_count = 0;

    return ok ? ZUNE_OK : ZUNE_BLOWN;
}

int zune_set_track_state(ZuneDevice *dev, uint32_t item_id,
                          uint16_t playcount, uint8_t rating) {
    zune_clear_error();
    if (!dev || item_id == 0) {
        zune_set_error("Invalid device or item_id");
        return ZUNE_BLOWN;
    }

    int ret = ZUNE_OK;

    if (mtp_set_object_prop_value_u32(&dev->ptp, item_id,
                                       MTP_OPC_UseCount,
                                       (uint32_t)playcount) != 0) {
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "set UseCount failed for %u (may be read-only)",
                item_id);
        ret = ZUNE_BLOWN;
    }

    if (mtp_set_object_prop_value_u16(&dev->ptp, item_id,
                                       MTP_OPC_Rating,
                                       (uint16_t)rating) != 0) {
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "set Rating failed for %u (may be read-only)",
                item_id);
        ret = ZUNE_BLOWN;
    }

    return ret;
}

/*
 * zune_probe_object — existence + size check for a DB audit.
 *
 * GetObjectInfo on the item: returns 0 if the object is real, -1 if
 * the device can't produce it (a ZMDB row pointing at a missing or
 * unreadable object — the kind of stranded entry that crash-loops the
 * firmware at playback). Size is ObjectCompressedSize (u32 on the
 * wire; Zune media never exceeds 4GB).
 */
int zune_probe_object(ZuneDevice *dev, uint32_t item_id,
                      uint64_t *out_size, uint16_t *out_format) {
    zune_clear_error();
    return zune_probe_object_named(dev, item_id, out_size, out_format, NULL);
}

/* As zune_probe_object, plus the object's FILENAME (caller frees).
 * Used to backfill photo names the Classic ZMDB parse can't resolve. */
int zune_probe_object_named(ZuneDevice *dev, uint32_t item_id,
                            uint64_t *out_size, uint16_t *out_format,
                            char **out_name) {
    zune_clear_error();
    if (!dev) return ZUNE_BLOWN;
    mtp_object_info_t info;
    if (mtp_get_object_info(&dev->ptp, item_id, &info) != 0)
        return ZUNE_BLOWN;
    if (out_size)   *out_size   = info.object_compressed_size;
    if (out_format) *out_format = info.object_format;
    if (out_name) {
        *out_name = info.filename ? strdup(info.filename) : NULL;
    }
    mtp_free_object_info(&info);
    return ZUNE_OK;
}

/*
 * zune_get_item_refs — raw GetObjectReferences (0x9810) for audits.
 * Playlist/album objects reference their members here; dangling refs
 * to purged items are a playback-crash suspect. Caller frees out_refs.
 */
int zune_get_item_refs(ZuneDevice *dev, uint32_t item_id,
                       uint32_t **out_refs, int *out_count) {
    zune_clear_error();
    if (!dev || !out_refs || !out_count) return ZUNE_BLOWN;
    *out_refs = NULL;
    *out_count = 0;
    return mtp_get_object_references(&dev->ptp, item_id, out_refs, out_count);
}
