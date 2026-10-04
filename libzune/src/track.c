/*
 * track.c — Music track operations (list, send, delete, download)
 *
 * Part of libzune. Pure C, no GTK/GLib dependencies.
 */

#include "zune_internal.h"

/*
 * Helper: read a UTF-8 string from an MTP object property.
 * Returns malloc'd string (caller frees), or NULL on failure.
 */
static char *read_string_prop(ptp_session_t *ptp, uint32_t handle, uint16_t prop) {
    uint8_t *data = NULL;
    uint32_t len = 0;
    if (mtp_get_object_prop_value(ptp, handle, prop, &data, &len) == 0
        && data && len > 0) {
        char *str = mtp_ucs2_to_string(data, len);
        free(data);
        return str;
    }
    free(data);
    return NULL;
}

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
        uint32_t val;
        memcpy(&val, data, 4);
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
        uint16_t val;
        memcpy(&val, data, 2);
        free(data);
        return val;
    }
    free(data);
    return default_val;
}

/* ---- Get all tracks ---- */

ZuneTrack *zune_get_tracks(ZuneDevice *dev, int *count) {
    if (!dev || !count) {
        zune_set_error("Invalid device or count pointer");
        if (count) *count = 0;
        return NULL;
    }

    *count = 0;

    if (dev->disconnecting) {
        zune_set_error("Device is disconnecting");
        return NULL;
    }

    /* Get handles for MP3 tracks */
    uint32_t *mp3_handles = NULL;
    int mp3_count = 0;
    mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_MP3,
                            0x00000000, &mp3_handles, &mp3_count);

    /* Get handles for WMA tracks */
    uint32_t *wma_handles = NULL;
    int wma_count = 0;
    mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_WMA,
                            0x00000000, &wma_handles, &wma_count);

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
        if (dev->disconnecting) break;

        uint32_t h = mp3_handles[i];
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, h, &info) != 0)
            continue;

        tracks[idx].item_id = h;
        tracks[idx].filesize = info.object_compressed_size;

        /* Read metadata properties */
        char *title = read_string_prop(&dev->ptp, h, MTP_OPC_Name);
        tracks[idx].title = title ? title : strdup(info.filename ? info.filename : "Unknown");

        char *artist = read_string_prop(&dev->ptp, h, MTP_OPC_Artist);
        tracks[idx].artist = artist ? artist : strdup("Unknown Artist");

        char *album = read_string_prop(&dev->ptp, h, MTP_OPC_AlbumName);
        tracks[idx].album = album ? album : strdup("Unknown Album");

        char *genre = read_string_prop(&dev->ptp, h, MTP_OPC_Genre);
        tracks[idx].genre = genre ? genre : strdup("Unknown");

        tracks[idx].duration_ms = read_u32_prop(&dev->ptp, h, MTP_OPC_Duration, 0);
        tracks[idx].tracknumber = read_u16_prop(&dev->ptp, h, MTP_OPC_Track, 0);

        mtp_free_object_info(&info);
        idx++;
    }

    /* Process WMA handles */
    for (int i = 0; i < wma_count && idx < n; i++) {
        if (dev->disconnecting) break;

        uint32_t h = wma_handles[i];
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, h, &info) != 0)
            continue;

        tracks[idx].item_id = h;
        tracks[idx].filesize = info.object_compressed_size;

        char *title = read_string_prop(&dev->ptp, h, MTP_OPC_Name);
        tracks[idx].title = title ? title : strdup(info.filename ? info.filename : "Unknown");

        char *artist = read_string_prop(&dev->ptp, h, MTP_OPC_Artist);
        tracks[idx].artist = artist ? artist : strdup("Unknown Artist");

        char *album = read_string_prop(&dev->ptp, h, MTP_OPC_AlbumName);
        tracks[idx].album = album ? album : strdup("Unknown Album");

        char *genre = read_string_prop(&dev->ptp, h, MTP_OPC_Genre);
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
 * Helper: extract filename without path and extension.
 * Returns malloc'd string. Caller frees.
 */
static char *filename_no_ext(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    const char *dot = strrchr(base, '.');
    if (dot && dot > base) {
        size_t len = (size_t)(dot - base);
        char *result = malloc(len + 1);
        if (result) {
            memcpy(result, base, len);
            result[len] = '\0';
        }
        return result;
    }
    return strdup(base);
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

    /* Step 1: SendObjectInfo */
    mtp_object_info_t info;
    memset(&info, 0, sizeof(info));
    info.storage_id = dev->storage_id;
    info.object_format = format;
    info.object_compressed_size = (uint32_t)fsize;
    info.parent_object = 0;
    info.filename = strdup(fname);

    uint32_t item_id = 0;
    int ret = -1;

    for (int attempt = 1; attempt <= 3; attempt++) {
        item_id = 0;

        if (zune_is_aborted(dev)) {
            free(info.filename);
            zune_set_error("Aborted: %s", filepath);
            return -1;
        }

        ret = mtp_send_object_info(&dev->ptp, dev->storage_id, 0, &info, &item_id);
        if (ret != 0) {
            fprintf(stderr, "[libzune] send_object_info attempt %d/3 failed for: %s (%s 0x%04X)\n",
                    attempt, filepath,
                    zune_autopsy_name(dev->ptp.last_response),
                    dev->ptp.last_response);
            if (zune_wound_is_mortal(dev->ptp.last_response))
                break;  /* device said no on purpose — retrying won't help */
            if (attempt < 3) {
                zune_unjam(dev);
                usleep(500000);
            }
            continue;
        }

        /* Step 2: SendObject (file data) */
        ret = mtp_send_object_from_file(&dev->ptp, filepath, (uint32_t)fsize);
        if (ret == 0)
            break;

        fprintf(stderr, "[libzune] send_object attempt %d/3 failed for: %s (%s 0x%04X)\n",
                attempt, filepath,
                zune_autopsy_name(dev->ptp.last_response),
                dev->ptp.last_response);

        /* Clean up orphan handle if object was partially created */
        if (item_id != 0) {
            mtp_delete_object(&dev->ptp, item_id);
            item_id = 0;
        }

        if (zune_wound_is_mortal(dev->ptp.last_response))
            break;

        if (attempt < 3) {
            zune_unjam(dev);
            usleep(500000);
        }
    }

    free(info.filename);

    if (ret != 0) {
        uint16_t rc = dev->ptp.last_response;
        zune_set_error("Failed to send track (%s 0x%04X): %s",
                       zune_autopsy_name(rc), rc, filepath);
        return -1;
    }

    /* Step 3: Set metadata properties
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
        fprintf(stderr, "[libzune] track %u: %d metadata propert%s failed to set\n",
                item_id, prop_fails, prop_fails == 1 ? "y" : "ies");

    if (out_item_id) *out_item_id = item_id;
    return 0;
}

int zune_smuggle_track(ZuneDevice *dev, const char *filepath,
                    uint32_t *out_item_id) {
    if (!dev || !filepath) {
        zune_set_error("Invalid device or filepath");
        return -1;
    }

    char *fallback_title = filename_no_ext(filepath);
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
    if (!dev || !filepath) {
        zune_set_error("Invalid device or filepath");
        return -1;
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
    if (!dev || !filepath) {
        zune_set_error("Invalid device or filepath");
        return -1;
    }

    /*
     * The native MTP stack's mtp_send_object_from_file reads the entire file
     * into memory and sends it in one bulk transfer. Progress callbacks at the
     * MTP object level are not currently supported — the transfer is atomic.
     *
     * Call the progress callback at start (0%) and end (100%) so callers
     * can still update their UI. When per-chunk progress is added to
     * mtp_send_object_from_file, this will be wired through.
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
    if (!dev) {
        zune_set_error("Invalid device");
        return -1;
    }

    int ret = mtp_delete_object(&dev->ptp, item_id);
    if (ret != 0) {
        zune_set_error("Failed to delete track %u", item_id);
        return -1;
    }
    return 0;
}

/* ---- Download track ---- */

int zune_extract_track(ZuneDevice *dev, uint32_t item_id,
                         const char *dest_path) {
    if (!dev || !dest_path) {
        zune_set_error("Invalid device or destination path");
        return -1;
    }

    int ret = mtp_get_object_to_file(&dev->ptp, item_id, dest_path);
    if (ret != 0) {
        zune_set_error("Failed to download track %u to %s", item_id, dest_path);
        return -1;
    }
    return 0;
}

/* ---- Track verification after upload ---- */

int zune_verify(ZuneDevice *dev, uint32_t item_id,
                 const char *expected_title, const char *expected_artist,
                 uint64_t expected_size) {
    if (!dev || item_id == 0) {
        zune_set_error("Invalid device or item_id for verification");
        return -1;
    }

    int mismatches = 0;

    /* Verify filesize via GetObjectInfo */
    if (expected_size > 0) {
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, item_id, &info) == 0) {
            if ((uint64_t)info.object_compressed_size != expected_size) {
                fprintf(stderr, "[libzune] verify: size mismatch for %u: "
                        "expected %llu, got %u\n", item_id,
                        (unsigned long long)expected_size,
                        info.object_compressed_size);
                mismatches++;
            }
            mtp_free_object_info(&info);
        } else {
            fprintf(stderr, "[libzune] verify: GetObjectInfo failed for %u\n",
                    item_id);
            mismatches++;
        }
    }

    /* Verify title */
    if (expected_title) {
        char *actual = read_string_prop(&dev->ptp, item_id, MTP_OPC_Name);
        if (!actual || strcmp(actual, expected_title) != 0) {
            fprintf(stderr, "[libzune] verify: title mismatch for %u: "
                    "expected '%s', got '%s'\n", item_id,
                    expected_title, actual ? actual : "(null)");
            mismatches++;
        }
        free(actual);
    }

    /* Verify artist */
    if (expected_artist) {
        char *actual = read_string_prop(&dev->ptp, item_id, MTP_OPC_Artist);
        if (!actual || strcmp(actual, expected_artist) != 0) {
            fprintf(stderr, "[libzune] verify: artist mismatch for %u: "
                    "expected '%s', got '%s'\n", item_id,
                    expected_artist, actual ? actual : "(null)");
            mismatches++;
        }
        free(actual);
    }

    if (mismatches > 0) {
        zune_set_error("Verification failed: %d mismatches for item %u",
                       mismatches, item_id);
        return -1;
    }

    return 0;
}

/* ---- Track user state (playcount / rating) ---- */

int zune_get_track_state(ZuneDevice *dev, uint32_t item_id,
                          uint16_t *out_playcount, uint8_t *out_rating,
                          uint32_t *out_skip_count) {
    if (!dev || item_id == 0) {
        zune_set_error("Invalid device or item_id");
        return -1;
    }

    int ok = 0;

    if (out_playcount) {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (mtp_get_object_prop_value(&dev->ptp, item_id, MTP_OPC_UseCount,
                                       &data, &len) == 0 && data && len >= 4) {
            *out_playcount = (uint16_t)(data[0] | (data[1] << 8));
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

    return ok ? 0 : -1;
}

int zune_set_track_state(ZuneDevice *dev, uint32_t item_id,
                          uint16_t playcount, uint8_t rating) {
    if (!dev || item_id == 0) {
        zune_set_error("Invalid device or item_id");
        return -1;
    }

    int ret = 0;

    if (mtp_set_object_prop_value_u32(&dev->ptp, item_id,
                                       MTP_OPC_UseCount,
                                       (uint32_t)playcount) != 0) {
        fprintf(stderr, "[libzune] set UseCount failed for %u (may be read-only)\n",
                item_id);
        ret = -1;
    }

    if (mtp_set_object_prop_value_u16(&dev->ptp, item_id,
                                       MTP_OPC_Rating,
                                       (uint16_t)rating) != 0) {
        fprintf(stderr, "[libzune] set Rating failed for %u (may be read-only)\n",
                item_id);
        ret = -1;
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
    return zune_probe_object_named(dev, item_id, out_size, out_format, NULL);
}

/* As zune_probe_object, plus the object's FILENAME (caller frees).
 * Used to backfill photo names the Classic ZMDB parse can't resolve. */
int zune_probe_object_named(ZuneDevice *dev, uint32_t item_id,
                            uint64_t *out_size, uint16_t *out_format,
                            char **out_name) {
    if (!dev) return -1;
    mtp_object_info_t info;
    if (mtp_get_object_info(&dev->ptp, item_id, &info) != 0)
        return -1;
    if (out_size)   *out_size   = info.object_compressed_size;
    if (out_format) *out_format = info.object_format;
    if (out_name) {
        *out_name = info.filename ? strdup(info.filename) : NULL;
    }
    mtp_free_object_info(&info);
    return 0;
}

/*
 * zune_get_item_refs — raw GetObjectReferences (0x9810) for audits.
 * Playlist/album objects reference their members here; dangling refs
 * to purged items are a playback-crash suspect. Caller frees out_refs.
 */
int zune_get_item_refs(ZuneDevice *dev, uint32_t item_id,
                       uint32_t **out_refs, int *out_count) {
    if (!dev || !out_refs || !out_count) return -1;
    *out_refs = NULL;
    *out_count = 0;
    return mtp_get_object_references(&dev->ptp, item_id, out_refs, out_count);
}
