/*
 * libzune — video.c
 * Video operations: enumerate, send (movie/tv/music video), delete, download.
 *
 * Uses the native PTP/MTP stack (mtp.h/ptp.h) instead of vendored libmtp.
 */

#include "zune_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* strcasecmp */
#include <sys/stat.h>
#include <time.h>

/* ---- UCS-2 helpers (local implementations) ----
 *
 * The description property (0xDC48) on the Zune requires AUINT16 format.
 * mtp_set_object_prop_value_ucs2() handles the wire encoding, but we
 * still need to convert UTF-8 to UCS-2 code units first.
 */
static uint16_t *simple_utf8_to_ucs2(const char *utf8, int *out_len) {
    if (!utf8) { *out_len = 0; return NULL; }
    size_t slen = strlen(utf8);
    uint16_t *buf = malloc((slen + 1) * sizeof(uint16_t));
    if (!buf) { *out_len = 0; return NULL; }
    int i = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p; ) {
        if (*p < 0x80) {
            buf[i++] = *p++;
        } else if ((*p & 0xE0) == 0xC0 && p[1]) {
            buf[i++] = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            buf[i++] = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else {
            buf[i++] = '?'; p++;
        }
    }
    buf[i] = 0;
    *out_len = i;
    return buf;
}

/* ---- helpers ---- */

/*
 * Check if an MTP object format code is a video type.
 * MTP_OFC_WMV (0xB981), MTP_OFC_MP4 (0xB982), ASF (0x300D), 3GP (0xB984).
 */
static int is_video_format(uint16_t fmt) {
    return fmt == MTP_OFC_WMV     /* 0xB981 */
        || fmt == MTP_OFC_MP4     /* 0xB982 */
        || fmt == 0x300D          /* ASF */
        || fmt == 0xB984;         /* 3GP */
}

/*
 * Determine MTP object format code from file extension.
 */
static uint16_t format_from_extension(const char *filepath) {
    const char *dot = strrchr(filepath, '.');
    if (dot) {
        if (strcasecmp(dot, ".wmv") == 0) return MTP_OFC_WMV;
        if (strcasecmp(dot, ".avi") == 0) return 0x300D;  /* ASF */
        if (strcasecmp(dot, ".mpeg") == 0 || strcasecmp(dot, ".mpg") == 0)
            return 0x300D;  /* ASF */
    }
    return MTP_OFC_MP4;
}

/*
 * Send JPEG data as an MTP representative sample (poster / album art).
 * Returns 0 on success, -1 on failure.
 */
static int send_representative_sample(ptp_session_t *ptp,
                                       uint32_t item_id,
                                       const uint8_t *jpeg_data,
                                       size_t jpeg_len) {
    if (!ptp || !jpeg_data || jpeg_len == 0) return -1;

    int ret = mtp_send_representative_sample(ptp, item_id,
                                              MTP_OFC_JPEG, 0, 0,
                                              jpeg_data, (uint32_t)jpeg_len);
    if (ret != 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "send_representative_sample: failed for item %u",
                item_id);
        return -1;
    }
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "representative sample sent for item %u (%zu bytes)",
            item_id, jpeg_len);
    return 0;
}

/*
 * Common post-send metadata setter: MetaGenre, description (UCS-2), Name.
 * Called after the file has been sent to the device. Returns failed fields.
 */
enum {
    VIDEO_FAIL_CATEGORY = 1 << 0, VIDEO_FAIL_DESCRIPTION = 1 << 1,
    VIDEO_FAIL_TITLE = 1 << 2, VIDEO_FAIL_SERIES = 1 << 3,
    VIDEO_FAIL_SEASON = 1 << 4, VIDEO_FAIL_EPISODE = 1 << 5,
    VIDEO_FAIL_POSTER = 1 << 6
};

static int set_video_metadata(ptp_session_t *ptp, uint32_t item_id,
                               uint16_t meta_genre,
                               const char *display_name,
                               const char *description) {
    /* MetaGenre -- controls TV / Movie / Music Video / Other on the Zune */
    int mr = mtp_set_object_prop_value_u16(ptp, item_id,
                                            MTP_OPC_MetaGenre, meta_genre);
    int failures = mr == 0 ? 0 : VIDEO_FAIL_CATEGORY;
    ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "MetaGenre 0x%02X: %s",
            meta_genre, mr == 0 ? "OK" : "FAILED");

    /* Description -- Zune requires UCS-2LE (AUINT16) for property 0xDC48.
     * UTF-8 string setter fails; must send as uint16_t array. */
    if (description && description[0]) {
        int len = 0;
        uint16_t *ucs2 = simple_utf8_to_ucs2(description, &len);
        if (ucs2 && len > 0) {
            int dr = mtp_set_object_prop_value_ucs2(ptp, item_id,
                                                     MTP_OPC_Description,
                                                     ucs2, (uint32_t)len);
            ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "Description (0xDC48): %s (%d chars)",
                    dr == 0 ? "OK" : "FAILED", len);
            if (dr != 0) failures |= VIDEO_FAIL_DESCRIPTION;
        } else failures |= VIDEO_FAIL_DESCRIPTION;
        free(ucs2);
    }

    /* Name property (0xDC44) -- display title */
    if (display_name && display_name[0]) {
        int nr = mtp_set_object_prop_value_str(ptp, item_id,
                                                MTP_OPC_Name, display_name);
        ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "Name '%s': %s",
                display_name, nr == 0 ? "OK" : "FAILED");
        if (nr != 0) failures |= VIDEO_FAIL_TITLE;
    }

    return failures;
}

static int set_video_series(ptp_session_t *ptp, uint32_t item_id,
                            const char *series, int season, int episode) {
    int failures = 0;
    int sr = mtp_set_object_prop_value_str(ptp, item_id,
                                           ZUNE_OPC_SeriesName, series);
    int snr = mtp_set_object_prop_value_u32(ptp, item_id,
                                           ZUNE_OPC_Season, (uint32_t)season);
    int enr = mtp_set_object_prop_value_u32(ptp, item_id,
                                           ZUNE_OPC_Episode, (uint32_t)episode);
    if (sr != 0) failures |= VIDEO_FAIL_SERIES;
    if (snr != 0) failures |= VIDEO_FAIL_SEASON;
    if (enr != 0) failures |= VIDEO_FAIL_EPISODE;
    ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "TV metadata '%s' S%02d E%02d: series=%s season=%s episode=%s",
            series, season, episode, sr == 0 ? "OK" : "FAILED",
            snr == 0 ? "OK" : "FAILED", enr == 0 ? "OK" : "FAILED");
    return failures;
}

/*
 * Send a video file to the device.  Shared core for movie / tv / music video.
 * Returns 0 on success, -1 on failure.  Writes item_id to *out_item_id.
 */
static int send_video_file(ZuneDevice *dev, const char *filepath,
                            const char *display_name,
                            uint32_t *out_item_id) {
    struct stat fst;
    if (stat(filepath, &fst) != 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "send_video_file: stat failed: %s", filepath);
        zune_set_error("Cannot read video file: %s", filepath);
        return -1;
    }
    if ((uint64_t)fst.st_size > 0xFFFFFFFFULL) {
        zune_set_error("File too large for MTP (%llu bytes, max 4 GiB): %s",
                       (unsigned long long)fst.st_size, filepath);
        return -1;
    }

    /* Determine format from extension */
    uint16_t fmt = format_from_extension(filepath);

    /* Build object info */
    mtp_object_info_t info;
    memset(&info, 0, sizeof(info));
    info.storage_id             = dev->storage_id;
    info.object_format          = fmt;
    info.object_compressed_size = (uint32_t)fst.st_size;
    info.parent_object          = 0;
    info.filename               = strdup(display_name ? display_name : "video");
    if (!info.filename) {
        zune_set_error("Cannot allocate video filename");
        return -1;
    }

    ZLOG(ZUNE_CHATTER_TALK, "libzune", "sending video: %s (%lu bytes, format=0x%04X)",
            display_name ? display_name : filepath,
            (unsigned long)fst.st_size, (int)fmt);

    zune_upload_t up = { .kind = "video", .path = filepath, .size = (uint32_t)fst.st_size,
                         .parent = 0, .info = &info };
    uint32_t item_id = 0;
    int status = zune_smuggle_core(dev, &up, &item_id);
    free(info.filename);
    if (status != ZUNE_UPLOAD_SENT) {
        if (out_item_id) *out_item_id = 0;
        return -1;          /* error set and zune_autopsy kept by the engine */
    }
    if (out_item_id) *out_item_id = item_id;

    ZLOG(ZUNE_CHATTER_TALK, "libzune", "video sent, item_id=%u", item_id);
    return 0;
}

/* ---- public API ---- */

ZuneVideoFile *zune_get_videos(ZuneDevice *dev, int *count) {
    zune_clear_error();
    if (!dev || !count) { if (count) *count = 0; return NULL; }
    *count = 0;

    if (ZUNE_FLAG_GET(dev->disconnecting)) return NULL;

    ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "zune_get_videos: fetching object handles...");

    /* Get all object handles (all formats, all parents). A failure
     * here is usually a transport desync (short read → 0x02FF) that
     * poisons the rest of the breach — clear the endpoint stalls and
     * retry ONCE before giving up (Pavo freeze, 2026-08-31). */
    uint32_t *handles = NULL;
    int nhandles = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id,
                                0x00000000, 0x00000000,
                                &handles, &nhandles) < 0 || nhandles == 0) {
        free(handles);
        handles = NULL;
        nhandles = 0;
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "zune_get_videos: handle query failed — "
                "unjamming and retrying once");
        zune_unjam(dev);
        if (mtp_get_object_handles(&dev->ptp, dev->storage_id,
                                    0x00000000, 0x00000000,
                                    &handles, &nhandles) < 0 || nhandles == 0) {
            free(handles);
            return NULL;
        }
    }

    /* First pass: count video files by checking object info. Bail on
     * a dead transport — each failed call burns a full USB timeout. */
    int n = 0;
    int fail_streak = 0;
    for (int i = 0; i < nhandles; i++) {
        if (ZUNE_FLAG_GET(dev->disconnecting)) break;
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, handles[i], &info) == 0) {
            if (is_video_format(info.object_format)) n++;
            mtp_free_object_info(&info);
            fail_streak = 0;
        } else if (++fail_streak >= 2) {
            ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "zune_get_videos: transport looks "
                    "dead, stopping count at %d/%d", i + 1, nhandles);
            break;
        }
    }
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "zune_get_videos: %d video files, reading MetaGenre...", n);

    if (n == 0) {
        free(handles);
        return NULL;
    }

    ZuneVideoFile *videos = calloc(n, sizeof(ZuneVideoFile));
    if (!videos) {
        free(handles);
        return NULL;
    }

    /* Second pass: collect video info and read MetaGenre */
    int idx = 0;
    fail_streak = 0;
    for (int i = 0; i < nhandles && idx < n; i++) {
        if (ZUNE_FLAG_GET(dev->disconnecting)) break;
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, handles[i], &info) != 0) {
            if (++fail_streak >= 2) {
                ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "zune_get_videos: transport looks "
                        "dead, stopping at %d/%d", i + 1, nhandles);
                break;
            }
            continue;
        }
        fail_streak = 0;
        if (!is_video_format(info.object_format)) {
            mtp_free_object_info(&info);
            continue;
        }

        videos[idx].item_id   = handles[i];
        videos[idx].filename  = strdup(info.filename ? info.filename : "Unknown");
        videos[idx].object_filename = strdup(info.filename ? info.filename : "");
        videos[idx].title = mtp_read_string_prop(&dev->ptp, handles[i], MTP_OPC_Name);
        if (!videos[idx].title) videos[idx].title = strdup("");
        videos[idx].filesize  = (uint64_t)info.object_compressed_size;
        videos[idx].parent_id = info.parent_object;
        videos[idx].object_format = info.object_format;

        /* Read MetaGenre -- each read is a slow MTP round-trip */
        uint8_t *prop_data = NULL;
        uint32_t prop_len = 0;
        uint16_t metagenre = 0;
        if (mtp_get_object_prop_value(&dev->ptp, handles[i],
                                       MTP_OPC_MetaGenre,
                                       &prop_data, &prop_len) == 0) {
            if (prop_data && prop_len >= 2) {
                metagenre = zune_le16(prop_data);
            }
            free(prop_data);
        }
        videos[idx].metagenre = metagenre;
        ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "MetaGenre for %d/%d id=%u -> 0x%02X",
                idx + 1, n, handles[i], metagenre);

        mtp_free_object_info(&info);
        idx++;
    }

    free(handles);

    *count = idx;
    return videos;
}

void zune_free_videos(ZuneVideoFile *videos, int count) {
    if (!videos) return;
    for (int i = 0; i < count; i++) {
        free(videos[i].filename);
        free(videos[i].title);
        free(videos[i].object_filename);
    }
    free(videos);
}

int zune_smuggle_video_named(ZuneDevice *dev, const char *filepath,
                            const char *object_filename, const char *title,
                            uint16_t meta_genre,
                            const char *series, int season, int episode,
                            const char *description,
                            const uint8_t *poster_jpeg, size_t poster_len,
                            uint32_t *out_item_id) {
    zune_clear_error();
    if (out_item_id) *out_item_id = 0;
    if (!dev || !filepath || !filepath[0] || !object_filename || !object_filename[0]
            || !title || !title[0] || !out_item_id || season < 0 || episode < 0
            || (poster_len && !poster_jpeg) || poster_len > UINT32_MAX - 4u
            || (meta_genre != ZUNE_METAGENRE_MOVIE && meta_genre != ZUNE_METAGENRE_TV_SHOW
                && meta_genre != ZUNE_METAGENRE_MUSIC_VIDEO && meta_genre != ZUNE_METAGENRE_OTHER)) {
        zune_set_error("Invalid named-video upload arguments");
        return ZUNE_BLOWN;
    }

    uint32_t item_id = 0;
    if (send_video_file(dev, filepath, object_filename, &item_id) != 0)
        return ZUNE_BLOWN;
    /* The media is complete. Never return a retryable file failure after here. */
    *out_item_id = item_id;
    int failures = set_video_metadata(&dev->ptp, item_id, meta_genre, title, description);
    if (meta_genre == ZUNE_METAGENRE_TV_SHOW && series && series[0])
        failures |= set_video_series(&dev->ptp, item_id, series, season, episode);
    if (poster_jpeg && poster_len > 0
            && send_representative_sample(&dev->ptp, item_id, poster_jpeg, poster_len) != 0)
        failures |= VIDEO_FAIL_POSTER;

    if (failures) {
        zune_set_error("Video uploaded (item %u), but could not save:%s%s%s%s%s%s%s. Do not upload again.",
                       item_id,
                       failures & VIDEO_FAIL_CATEGORY ? " category" : "",
                       failures & VIDEO_FAIL_TITLE ? " title" : "",
                       failures & VIDEO_FAIL_DESCRIPTION ? " description" : "",
                       failures & VIDEO_FAIL_SERIES ? " series" : "",
                       failures & VIDEO_FAIL_SEASON ? " season" : "",
                       failures & VIDEO_FAIL_EPISODE ? " episode" : "",
                       failures & VIDEO_FAIL_POSTER ? " poster" : "");
        return ZUNE_VIDEO_METADATA_INCOMPLETE;
    }
    return ZUNE_OK;
}

int zune_smuggle_movie(ZuneDevice *dev, const char *filepath,
                     const char *display_name, const char *description,
                     const uint8_t *poster_jpeg, size_t poster_len,
                     uint32_t *out_item_id) {
    zune_clear_error();
    if (!dev || !filepath) return ZUNE_BLOWN;

    uint32_t item_id = 0;
    if (send_video_file(dev, filepath, display_name, &item_id) != 0)
        return ZUNE_BLOWN;

    if (out_item_id) *out_item_id = item_id;

    /* MetaGenre = 0x25 (Movie), description, Name */
    set_video_metadata(&dev->ptp, item_id, ZUNE_METAGENRE_MOVIE,
                       display_name, description);

    /* Poster as representative sample */
    if (poster_jpeg && poster_len > 0)
        send_representative_sample(&dev->ptp, item_id, poster_jpeg, poster_len);

    return ZUNE_OK;
}

int zune_get_series_info(ZuneDevice *dev, uint32_t item_id,
                                char **out_series, int *out_season, int *out_episode,
                                char **out_title)
{
    zune_clear_error();
    if (!dev) return ZUNE_BLOWN;

    /* Read series name (0xDA9A) -- string property */
    char *series = mtp_read_string_prop(&dev->ptp, item_id, ZUNE_OPC_SeriesName);

    /* Read season (0xDAB5) -- uint32 property */
    uint32_t season = 0;
    {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (mtp_get_object_prop_value(&dev->ptp, item_id, ZUNE_OPC_Season,
                                       &data, &len) == 0) {
            if (data && len >= 4) {
                season = zune_le32(data);
            }
            free(data);
        }
    }

    /* Read episode (0xDAB6) -- uint32 property */
    uint32_t episode = 0;
    {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (mtp_get_object_prop_value(&dev->ptp, item_id, ZUNE_OPC_Episode,
                                       &data, &len) == 0) {
            if (data && len >= 4) {
                episode = zune_le32(data);
            }
            free(data);
        }
    }

    /* Display title is Name; filename is only a fallback for missing metadata. */
    char *title = mtp_read_string_prop(&dev->ptp, item_id, MTP_OPC_Name);
    if (!title || !title[0]) {
        free(title);
        title = mtp_read_string_prop(&dev->ptp, item_id, MTP_OPC_ObjectFileName);
    }

    const int found = series && series[0];
    if (out_series)  *out_series  = series; else free(series);
    if (out_season)  *out_season  = (int)season;
    if (out_episode) *out_episode = (int)episode;
    if (out_title)   *out_title   = title;  else free(title);

    return found ? ZUNE_OK : ZUNE_BLOWN;
}

int zune_smuggle_episode(ZuneDevice *dev, const char *filepath,
                          const char *display_name,
                          const char *series, int season, int episode,
                          const char *description,
                          const uint8_t *poster_jpeg, size_t poster_len,
                          uint32_t *out_item_id) {
    zune_clear_error();
    if (!dev || !filepath) return ZUNE_BLOWN;

    uint32_t item_id = 0;
    if (send_video_file(dev, filepath, display_name, &item_id) != 0)
        return ZUNE_BLOWN;

    if (out_item_id) *out_item_id = item_id;

    /* MetaGenre = 0x26 (TV Show) */
    set_video_metadata(&dev->ptp, item_id, ZUNE_METAGENRE_TV_SHOW,
                       NULL, description);

    /* Set Name as "Series - S02E05" */
    if (series && series[0] && episode > 0) {
        char title_buf[256];
        snprintf(title_buf, sizeof(title_buf), "%s - S%02dE%02d",
                 series, season, episode);
        int nr = mtp_set_object_prop_value_str(&dev->ptp, item_id,
                                                MTP_OPC_Name, title_buf);
        ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "Name '%s': %s",
                title_buf, nr == 0 ? "OK" : "FAILED");
    }

    /* Vendor properties for TV metadata:
     *   0xDA9A (STRING) -- series name
     *   0xDAB5 (UINT32) -- season number
     *   0xDAB6 (UINT32) -- episode number */
    if (series && series[0] && episode > 0) {
        set_video_series(&dev->ptp, item_id, series, season, episode);
    }

    /* Poster as representative sample */
    if (poster_jpeg && poster_len > 0)
        send_representative_sample(&dev->ptp, item_id, poster_jpeg, poster_len);

    return ZUNE_OK;
}

int zune_smuggle_clip(ZuneDevice *dev, const char *filepath,
                           const char *display_name,
                           const uint8_t *poster_jpeg, size_t poster_len,
                           uint32_t *out_item_id) {
    zune_clear_error();
    if (!dev || !filepath) return ZUNE_BLOWN;

    uint32_t item_id = 0;
    if (send_video_file(dev, filepath, display_name, &item_id) != 0)
        return ZUNE_BLOWN;

    if (out_item_id) *out_item_id = item_id;

    /* MetaGenre = 0x23 (Music Video), Name, NO vendor props */
    set_video_metadata(&dev->ptp, item_id, ZUNE_METAGENRE_MUSIC_VIDEO,
                       display_name, NULL);

    /* Poster as representative sample */
    if (poster_jpeg && poster_len > 0)
        send_representative_sample(&dev->ptp, item_id, poster_jpeg, poster_len);

    return ZUNE_OK;
}

int zune_smuggle_other(ZuneDevice *dev, const char *filepath,
                       const char *display_name, const char *description,
                       const uint8_t *poster_jpeg, size_t poster_len,
                       uint32_t *out_item_id) {
    zune_clear_error();
    if (!dev || !filepath) return ZUNE_BLOWN;

    uint32_t item_id = 0;
    if (send_video_file(dev, filepath, display_name, &item_id) != 0)
        return ZUNE_BLOWN;

    if (out_item_id) *out_item_id = item_id;

    /* MetaGenre = 0x21 (Other), description, Name */
    set_video_metadata(&dev->ptp, item_id, ZUNE_METAGENRE_OTHER,
                       display_name, description);

    if (poster_jpeg && poster_len > 0)
        send_representative_sample(&dev->ptp, item_id, poster_jpeg, poster_len);

    return ZUNE_OK;
}

int zune_purge_video(ZuneDevice *dev, uint32_t item_id) {
    zune_clear_error();
    if (!dev) {
        zune_set_error("Invalid device");
        return ZUNE_BLOWN;
    }
    int ret = mtp_delete_object(&dev->ptp, item_id);
    if (ret != 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "delete video failed: item %u", item_id);
        zune_set_error("Failed to delete video %u (%s 0x%04X)", item_id,
                       zune_autopsy_name(dev->ptp.last_response), dev->ptp.last_response);
        return ZUNE_BLOWN;
    }
    return ZUNE_OK;
}

int zune_extract_video(ZuneDevice *dev, uint32_t item_id,
                         const char *dest_path) {
    zune_clear_error();
    if (!dev || !dest_path) {
        zune_set_error("Invalid device or destination path");
        return ZUNE_BLOWN;
    }
    int ret = mtp_get_object_to_file(&dev->ptp, item_id, dest_path);
    if (ret != 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "download video failed: item %u", item_id);
        zune_set_error("Failed to download video %u to %s", item_id, dest_path);
        return ZUNE_BLOWN;
    }
    return ZUNE_OK;
}
