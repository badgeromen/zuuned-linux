/*
 * libzune — photo.c
 * Photo operations: enumerate, albums, send, delete, download.
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

/* ---- internal folder name cache (per device) ---- */

typedef struct ZuneFolderCacheEntry {
    uint32_t folder_id;
    char *name;
    struct ZuneFolderCacheEntry *next;
} FolderEntry;

void zune_free_folder_cache(ZuneDevice *dev) {
    if (!dev) return;
    FolderEntry *e = dev->folder_cache;
    while (e) {
        FolderEntry *next = e->next;
        free(e->name);
        free(e);
        e = next;
    }
    dev->folder_cache = NULL;
}

static void folder_cache_insert(ZuneDevice *dev, uint32_t folder_id, const char *name) {
    FolderEntry *e = malloc(sizeof(FolderEntry));
    if (!e) return;
    e->folder_id = folder_id;
    e->name = strdup(name ? name : "");
    e->next = dev->folder_cache;
    dev->folder_cache = e;
}

static const char *folder_cache_lookup(ZuneDevice *dev, uint32_t folder_id) {
    for (FolderEntry *e = dev->folder_cache; e; e = e->next) {
        if (e->folder_id == folder_id)
            return e->name;
    }
    return NULL;
}

static int folder_cache_count(ZuneDevice *dev) {
    int n = 0;
    for (FolderEntry *e = dev->folder_cache; e; e = e->next) n++;
    return n;
}

/*
 * Cache folder names from the device (call during connect or first photo op).
 * After this, zune_get_photo_albums() works without USB calls.
 *
 * Uses mtp_get_object_handles with format=Association to get all folders,
 * then mtp_get_object_info to read each folder's name. Returns 0 when the
 * whole folder list was read (an empty list included), -1 when the handle
 * query or any folder read failed; the cache then holds only what was read.
 */
int zune_cache_folder_names(ZuneDevice *dev) {
    if (!dev) return -1;

    zune_free_folder_cache(dev);

    uint32_t *handles = NULL;
    int nhandles = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id,
                                MTP_OFC_Association, 0x00000000,
                                &handles, &nhandles) < 0) {
        free(handles);
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "folder list unavailable; cached 0 folder names");
        return -1;
    }

    int complete = 1;
    for (int i = 0; i < nhandles; i++) {
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, handles[i], &info) == 0) {
            if (info.filename)
                folder_cache_insert(dev, handles[i], info.filename);
            else
                complete = 0;
            mtp_free_object_info(&info);
        } else {
            complete = 0;
        }
    }

    free(handles);
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "cached %d folder names", folder_cache_count(dev));
    return complete ? 0 : -1;
}

/* Cached folder named exactly `name`, or 0. */
static uint32_t folder_cache_find(ZuneDevice *dev, const char *name) {
    for (FolderEntry *e = dev->folder_cache; e; e = e->next)
        if (e->name && strcmp(e->name, name) == 0)
            return e->folder_id;
    return 0;
}

/* 1 if the file starts with the JPEG SOI marker and a segment (FF D8 FF). */
static int file_is_jpeg(const char *path) {
    unsigned char magic[3];
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    size_t n = fread(magic, 1, sizeof(magic), fp);
    fclose(fp);
    return n == 3 && magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF;
}

/* ---- helpers ---- */

/*
 * Check if an MTP object format code is a picture type.
 * JPEG (0x3801) is the primary format; also check common image codes.
 */
static int is_picture_format(uint16_t fmt) {
    return fmt == MTP_OFC_JPEG   /* 0x3801 */
        || fmt == 0x3800         /* EXIF/JPEG */
        || fmt == 0x3804         /* BMP */
        || fmt == 0x3807         /* GIF */
        || fmt == 0x380B         /* PNG */
        || fmt == 0x380D;        /* TIFF */
}

/* ---- public API ---- */

ZunePhotoFile *zune_get_photos(ZuneDevice *dev, int *count) {
    zune_clear_error();
    if (!dev || !count) { if (count) *count = 0; return NULL; }
    *count = 0;

    if (ZUNE_FLAG_GET(dev->disconnecting)) return NULL;

    ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "zune_get_photos: fetching object handles...");

    /* Get all object handles (all formats, all parents) */
    uint32_t *handles = NULL;
    int nhandles = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id,
                                0x00000000, 0x00000000,
                                &handles, &nhandles) < 0 || nhandles == 0) {
        free(handles);
        return NULL;
    }

    /* First pass: count picture files */
    int n = 0;
    for (int i = 0; i < nhandles; i++) {
        if (ZUNE_FLAG_GET(dev->disconnecting)) break;
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, handles[i], &info) == 0) {
            if (is_picture_format(info.object_format)) n++;
            mtp_free_object_info(&info);
        }
    }
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "zune_get_photos: %d picture files", n);

    if (n == 0) {
        free(handles);
        return NULL;
    }

    ZunePhotoFile *photos = calloc(n, sizeof(ZunePhotoFile));
    if (!photos) {
        free(handles);
        return NULL;
    }

    /* Second pass: collect photo info */
    int idx = 0;
    for (int i = 0; i < nhandles && idx < n; i++) {
        if (ZUNE_FLAG_GET(dev->disconnecting)) break;
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, handles[i], &info) != 0) continue;
        if (!is_picture_format(info.object_format)) {
            mtp_free_object_info(&info);
            continue;
        }

        photos[idx].item_id   = handles[i];
        photos[idx].filename  = strdup(info.filename ? info.filename : "Unknown");
        photos[idx].filesize  = (uint64_t)info.object_compressed_size;
        photos[idx].parent_id = info.parent_object;
        /* NO MetaGenre read for pictures -- always 0, and each read
         * is a slow MTP round-trip (~1-2s per file) */

        mtp_free_object_info(&info);
        idx++;
    }

    free(handles);

    /* Cache folder names while we have access (for album grouping) */
    if (idx > 0)
        zune_cache_folder_names(dev);

    *count = idx;
    return photos;
}

void zune_free_photos(ZunePhotoFile *photos, int count) {
    if (!photos) return;
    for (int i = 0; i < count; i++)
        free(photos[i].filename);
    free(photos);
}

int zune_get_dimensions(ZuneDevice *dev, uint32_t item_id,
                               uint32_t *width, uint32_t *height)
{
    zune_clear_error();
    if (!dev || !width || !height) return ZUNE_BLOWN;

    /* Read width (0xDC87) */
    *width = 0;
    {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (mtp_get_object_prop_value(&dev->ptp, item_id, 0xDC87,
                                       &data, &len) == 0) {
            if (data && len >= 4) {
                *width = zune_le32(data);
            }
            free(data);
        }
    }

    /* Read height (0xDC88) */
    *height = 0;
    {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (mtp_get_object_prop_value(&dev->ptp, item_id, 0xDC88,
                                       &data, &len) == 0) {
            if (data && len >= 4) {
                *height = zune_le32(data);
            }
            free(data);
        }
    }

    if (*width == 0 || *height == 0) return ZUNE_BLOWN;
    return ZUNE_OK;
}

ZunePhotoAlbum *zune_get_photo_albums(ZuneDevice *dev,
                                       ZunePhotoFile *photos, int photo_count,
                                       int *album_count) {
    zune_clear_error();
    /* Names come from this device's folder cache -- no USB needed. */
    if (!album_count) {
        zune_set_error("Invalid album count output");
        return NULL;
    }
    *album_count = 0;
    if (!photos || photo_count == 0) return NULL;

    /* Aggregate photos by parent_id */
    typedef struct { uint32_t folder_id; int count; } FolderAgg;
    FolderAgg *aggs = NULL;
    int num = 0;

    for (int i = 0; i < photo_count; i++) {
        int found = 0;
        for (int j = 0; j < num; j++) {
            if (aggs[j].folder_id == photos[i].parent_id) {
                aggs[j].count++;
                found = 1;
                break;
            }
        }
        if (!found) {
            num++;
            FolderAgg *tmp = realloc(aggs, num * sizeof(FolderAgg));
            if (!tmp) { free(aggs); *album_count = 0; return NULL; }
            aggs = tmp;
            aggs[num - 1].folder_id = photos[i].parent_id;
            aggs[num - 1].count = 1;
        }
    }

    ZunePhotoAlbum *albums = calloc(num, sizeof(ZunePhotoAlbum));
    if (!albums) { free(aggs); return NULL; }

    for (int i = 0; i < num; i++) {
        albums[i].folder_id    = aggs[i].folder_id;
        albums[i].photo_count  = aggs[i].count;

        /* Look up folder name from cache */
        const char *name = dev ? folder_cache_lookup(dev, aggs[i].folder_id) : NULL;
        albums[i].name = strdup(name ? name : "Photos");
    }
    free(aggs);
    *album_count = num;

    ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "found %d photo albums (no USB call)", num);
    return albums;
}

void zune_free_photo_albums(ZunePhotoAlbum *albums, int count) {
    if (!albums) return;
    for (int i = 0; i < count; i++)
        free(albums[i].name);
    free(albums);
}

/* ---- Photo Preprocessing ----
 *
 * Resize to max 480px longest edge and convert to baseline JPEG.
 * The Zune's screen is 480x272 (HD) or 320x240 (Classic), and its
 * JPEG decoder can't handle high-resolution or HDR images.
 * The Windows Zune Software does this same preprocessing.
 *
 * macOS: uses sips (always available, no extra dependency)
 * Linux: uses ffmpeg (same dependency as transcode)
 */
char *zune_arm_photo(const char *input_path)
{
    zune_clear_error();
    if (!input_path) return NULL;

    char *output = zune_mktemp("photo", ".jpg");
    if (!output) return NULL;

#ifdef __APPLE__
    /* sips: -Z constrains longest edge, preserves aspect ratio */
    const char *tool = "sips";
    const char *const argv[] = {
        "sips", "-s", "format", "jpeg", "-s", "formatOptions", "90",
        "-Z", "480", input_path, "--out", output, NULL
    };
#else
    /* ffmpeg: scale to fit 480px, convert to JPEG. The filter keeps its own
     * ffmpeg quoting; it is a single argument, so no shell sees it.
     * -frames:v 1: an animated GIF/WebP has many frames, and ffmpeg refuses
     * to write more than one into a single .jpg; take the first. */
    const char *tool = "ffmpeg";
    const char *const argv[] = {
        "ffmpeg", "-y", "-i", input_path, "-vf",
        "scale='min(480,iw)':'min(480,ih)':force_original_aspect_ratio=decrease",
        "-frames:v", "1", "-q:v", "5", output, NULL
    };
#endif

    int status = zune_run_tool(tool, argv, -1);
    if (status != 0 || !zune_tool_output_ok(output)) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "photo prepare FAILED: %s", input_path);
        if (status >= 0)
            zune_set_error("%s could not prepare the photo (exit %d)", tool, status);
        unlink(output);
        free(output);
        return NULL;
    }

    ZLOG(ZUNE_CHATTER_TALK, "libzune", "photo prepared (480px JPEG): %s",
            strrchr(input_path, '/') ? strrchr(input_path, '/') + 1 : input_path);
    return output;
}

int zune_smuggle_photo(ZuneDevice *dev, const char *filepath,
                     const char *album_name) {
    zune_clear_error();
    if (!dev || !filepath) return ZUNE_BLOWN;

    /* Preprocess: resize + convert to JPEG for Zune compatibility. The
     * object is declared as JPEG, so the original may stand in only when it
     * already is one.
     *
     * The three refusals below happen before any device write, and Zuuned
     * (DeviceWorker::photoRefusedBeforeWrite) recognizes them by the start of
     * their messages to skip the photo without transfer recovery: keep
     * "Photo is not a JPEG", "Cannot read photo:" and "Could not read the
     * device's photo albums" as they are. */
    char *prepared = zune_arm_photo(filepath);
    if (!prepared && !file_is_jpeg(filepath)) {
        char cause[256];
        const char *why = zune_get_error();
        snprintf(cause, sizeof(cause), "%s", why ? why : "conversion failed");
        zune_set_error("Photo is not a JPEG and could not be converted (%s): %s",
                       cause, filepath);
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "send_photo: not a JPEG and conversion failed: %s",
                filepath);
        return ZUNE_BLOWN;
    }
    const char *send_path = prepared ? prepared : filepath;
    if (!prepared)
        zune_clear_error();   /* the original JPEG stands in; its conversion error is not this call's */

    struct stat fst;
    if (stat(send_path, &fst) != 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "send_photo: stat failed: %s", send_path);
        zune_set_error("Cannot read photo: %s", send_path);
        if (prepared) unlink(prepared);
        free(prepared);
        return ZUNE_BLOWN;
    }

    uint32_t folder_id = 0;

    /* If album_name is provided, find or create the folder */
    if (album_name && album_name[0]) {
        folder_id = folder_cache_find(dev, album_name);

        /* A fresh session has an empty cache: ask the device before
         * creating, or every session adds another folder with this name.
         * An unreadable folder list never justifies creating one. */
        if (folder_id == 0) {
            if (zune_cache_folder_names(dev) < 0) {
                zune_set_error("Could not read the device's photo albums to find '%s'",
                               album_name);
                if (prepared) { unlink(prepared); free(prepared); }
                return ZUNE_BLOWN;
            }
            folder_id = folder_cache_find(dev, album_name);
            if (folder_id)
                ZLOG(ZUNE_CHATTER_TALK, "libzune", "found photo album on device: %s (id=%u)",
                        album_name, folder_id);
        }

        /* Create if not found */
        if (folder_id == 0) {
            uint32_t new_handle = 0;
            if (mtp_create_folder(&dev->ptp, 0, dev->storage_id,
                                   album_name, &new_handle) < 0 || new_handle == 0) {
                ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "failed to create photo album: %s",
                        album_name);
                /* Fall through -- send to root */
            } else {
                folder_id = new_handle;
                ZLOG(ZUNE_CHATTER_TALK, "libzune", "created photo album: %s (id=%u)",
                        album_name, folder_id);
                /* Add to cache */
                folder_cache_insert(dev, folder_id, album_name);
            }
        }
    }

    /* Use original filename for the device, but .jpg extension since we converted */
    const char *orig_basename = strrchr(filepath, '/');
    orig_basename = orig_basename ? orig_basename + 1 : filepath;

    /* Build JPEG filename from original name */
    char jpg_name[256];
    const char *dot = strrchr(orig_basename, '.');
    if (dot && strcasecmp(dot, ".jpg") != 0 && strcasecmp(dot, ".jpeg") != 0) {
        size_t namelen = (size_t)(dot - orig_basename);
        snprintf(jpg_name, sizeof(jpg_name), "%.*s.jpg", (int)namelen, orig_basename);
    } else {
        snprintf(jpg_name, sizeof(jpg_name), "%s", orig_basename);
    }

    /* Build object info -- always send as JPEG since we preprocessed */
    mtp_object_info_t info;
    memset(&info, 0, sizeof(info));
    info.storage_id             = dev->storage_id;
    info.object_format          = MTP_OFC_JPEG;
    info.object_compressed_size = (uint32_t)fst.st_size;
    info.parent_object          = folder_id;
    info.filename               = strdup(jpg_name);

    zune_upload_t up = { .kind = "photo", .path = send_path, .source = filepath, .size = (uint32_t)fst.st_size,
                         .parent = folder_id, .info = &info };
    uint32_t new_handle = 0;
    int sent = zune_smuggle_core(dev, &up, &new_handle) == ZUNE_UPLOAD_SENT;
    free(info.filename);

    /* Clean up temp file */
    if (prepared) {
        unlink(prepared);
        free(prepared);
    }

    if (!sent) {
        /* The engine set the error and kept zune_autopsy on the failure. */
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "send_photo failed: %s", filepath);
        return ZUNE_BLOWN;
    }

    ZLOG(ZUNE_CHATTER_TALK, "libzune", "photo sent: %s -> folder %u", jpg_name, folder_id);
    return ZUNE_OK;
}

int zune_purge_photo(ZuneDevice *dev, uint32_t item_id) {
    zune_clear_error();
    if (!dev) {
        zune_set_error("Invalid device");
        return ZUNE_BLOWN;
    }
    int ret = mtp_delete_object(&dev->ptp, item_id);
    if (ret != 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "delete photo failed: item %u", item_id);
        zune_set_error("Failed to delete photo %u (%s 0x%04X)", item_id,
                       zune_autopsy_name(dev->ptp.last_response), dev->ptp.last_response);
        return ZUNE_BLOWN;
    }
    return ZUNE_OK;
}

int zune_extract_photo(ZuneDevice *dev, uint32_t item_id,
                         const char *dest_path) {
    zune_clear_error();
    if (!dev || !dest_path) {
        zune_set_error("Invalid device or destination path");
        return ZUNE_BLOWN;
    }
    int ret = mtp_get_object_to_file(&dev->ptp, item_id, dest_path);
    if (ret != 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "download photo failed: item %u", item_id);
        zune_set_error("Failed to download photo %u to %s", item_id, dest_path);
        return ZUNE_BLOWN;
    }
    return ZUNE_OK;
}
