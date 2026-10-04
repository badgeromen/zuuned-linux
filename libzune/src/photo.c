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

/* ---- internal folder name cache ---- */

typedef struct FolderEntry {
    uint32_t folder_id;
    char *name;
    struct FolderEntry *next;
} FolderEntry;

/* Module-level linked list -- simple replacement for GHashTable */
static FolderEntry *folder_cache = NULL;

static void folder_cache_clear(void) {
    FolderEntry *e = folder_cache;
    while (e) {
        FolderEntry *next = e->next;
        free(e->name);
        free(e);
        e = next;
    }
    folder_cache = NULL;
}

static void folder_cache_insert(uint32_t folder_id, const char *name) {
    FolderEntry *e = malloc(sizeof(FolderEntry));
    if (!e) return;
    e->folder_id = folder_id;
    e->name = strdup(name ? name : "");
    e->next = folder_cache;
    folder_cache = e;
}

static const char *folder_cache_lookup(uint32_t folder_id) {
    for (FolderEntry *e = folder_cache; e; e = e->next) {
        if (e->folder_id == folder_id)
            return e->name;
    }
    return NULL;
}

static int folder_cache_count(void) {
    int n = 0;
    for (FolderEntry *e = folder_cache; e; e = e->next) n++;
    return n;
}

/*
 * Cache folder names from the device (call during connect or first photo op).
 * After this, zune_get_photo_albums() works without USB calls.
 *
 * Uses mtp_get_object_handles with format=Association to get all folders,
 * then mtp_get_object_info to read each folder's name.
 */
void zune_cache_folder_names(ZuneDevice *dev) {
    if (!dev) return;

    folder_cache_clear();

    uint32_t *handles = NULL;
    int nhandles = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id,
                                MTP_OFC_Association, 0x00000000,
                                &handles, &nhandles) < 0 || nhandles == 0) {
        free(handles);
        fprintf(stderr, "[libzune] cached 0 folder names\n");
        return;
    }

    for (int i = 0; i < nhandles; i++) {
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, handles[i], &info) == 0) {
            if (info.filename)
                folder_cache_insert(handles[i], info.filename);
            mtp_free_object_info(&info);
        }
    }

    free(handles);
    fprintf(stderr, "[libzune] cached %d folder names\n", folder_cache_count());
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
    if (!dev || !count) { if (count) *count = 0; return NULL; }
    *count = 0;

    if (dev->disconnecting) return NULL;

    fprintf(stderr, "[libzune] zune_get_photos: fetching object handles...\n");

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
        if (dev->disconnecting) break;
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, handles[i], &info) == 0) {
            if (is_picture_format(info.object_format)) n++;
            mtp_free_object_info(&info);
        }
    }
    fprintf(stderr, "[libzune] zune_get_photos: %d picture files\n", n);

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
        if (dev->disconnecting) break;
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
    if (!dev || !width || !height) return -1;

    /* Read width (0xDC87) */
    *width = 0;
    {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (mtp_get_object_prop_value(&dev->ptp, item_id, 0xDC87,
                                       &data, &len) == 0) {
            if (data && len >= 4) {
                *width = (uint32_t)(data[0] | (data[1] << 8) |
                                    (data[2] << 16) | (data[3] << 24));
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
                *height = (uint32_t)(data[0] | (data[1] << 8) |
                                     (data[2] << 16) | (data[3] << 24));
            }
            free(data);
        }
    }

    if (*width == 0 || *height == 0) return -1;
    return 0;
}

ZunePhotoAlbum *zune_get_photo_albums(ZuneDevice *dev,
                                       ZunePhotoFile *photos, int photo_count,
                                       int *album_count) {
    (void)dev;  /* cached folder names -- no USB needed */
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
        const char *name = folder_cache_lookup(aggs[i].folder_id);
        albums[i].name = strdup(name ? name : "Photos");
    }
    free(aggs);
    *album_count = num;

    fprintf(stderr, "[libzune] found %d photo albums (no USB call)\n", num);
    return albums;
}

void zune_free_photo_albums(ZunePhotoAlbum *albums, int count) {
    if (!albums) return;
    for (int i = 0; i < count; i++)
        free(albums[i].name);
    free(albums);
}

/* Shell escaping for safe popen/system calls (duplicated from transcode.c) */
static char *shell_escape(const char *s)
{
    size_t len = strlen(s);
    char *buf = malloc(len * 4 + 1);
    if (!buf) return NULL;
    char *out = buf;
    for (const char *p = s; *p; p++) {
        if (*p == '\'') { *out++ = '\''; *out++ = '\\'; *out++ = '\''; *out++ = '\''; }
        else { *out++ = *p; }
    }
    *out = '\0';
    return buf;
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
    if (!input_path) return NULL;

    char output[512];
    snprintf(output, sizeof(output), "/tmp/zunelinux_photo_%d_%lu.jpg",
             (int)getpid(), (unsigned long)time(NULL));

    char *esc_in  = shell_escape(input_path);
    char *esc_out = shell_escape(output);
    if (!esc_in || !esc_out) {
        free(esc_in);
        free(esc_out);
        return NULL;
    }

    char cmd[2048];

#ifdef __APPLE__
    /* sips: -Z constrains longest edge, preserves aspect ratio */
    snprintf(cmd, sizeof(cmd),
        "sips -s format jpeg -s formatOptions 90 -Z 480 '%s' --out '%s' 2>/dev/null",
        esc_in, esc_out);
#else
    /* ffmpeg: scale to fit 480px, convert to JPEG */
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -i '%s' -vf \"scale='min(480,iw)':'min(480,ih)'"
        ":force_original_aspect_ratio=decrease\" "
        "-q:v 5 '%s' 2>/dev/null",
        esc_in, esc_out);
#endif

    free(esc_in);
    free(esc_out);

    int status = system(cmd);
    if (status != 0 || access(output, F_OK) != 0) {
        fprintf(stderr, "[libzune] photo prepare FAILED: %s\n", input_path);
        return NULL;
    }

    fprintf(stderr, "[libzune] photo prepared (480px JPEG): %s\n",
            strrchr(input_path, '/') ? strrchr(input_path, '/') + 1 : input_path);
    return strdup(output);
}

int zune_smuggle_photo(ZuneDevice *dev, const char *filepath,
                     const char *album_name) {
    if (!dev || !filepath) return -1;

    /* Preprocess: resize + convert to JPEG for Zune compatibility */
    char *prepared = zune_arm_photo(filepath);
    const char *send_path = prepared ? prepared : filepath;

    struct stat fst;
    if (stat(send_path, &fst) != 0) {
        fprintf(stderr, "[libzune] send_photo: stat failed: %s\n", send_path);
        free(prepared);
        return -1;
    }

    uint32_t folder_id = 0;

    /* If album_name is provided, find or create the folder */
    if (album_name && album_name[0]) {
        /* Search cache for existing folder with this name */
        for (FolderEntry *e = folder_cache; e; e = e->next) {
            if (e->name && strcmp(e->name, album_name) == 0) {
                folder_id = e->folder_id;
                break;
            }
        }

        /* Create if not found */
        if (folder_id == 0) {
            uint32_t new_handle = 0;
            if (mtp_create_folder(&dev->ptp, 0, dev->storage_id,
                                   album_name, &new_handle) < 0 || new_handle == 0) {
                fprintf(stderr, "[libzune] failed to create photo album: %s\n",
                        album_name);
                /* Fall through -- send to root */
            } else {
                folder_id = new_handle;
                fprintf(stderr, "[libzune] created photo album: %s (id=%u)\n",
                        album_name, folder_id);
                /* Add to cache */
                folder_cache_insert(folder_id, album_name);
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

    int ret = -1;
    uint32_t new_handle = 0;
    for (int attempt = 1; attempt <= 3; attempt++) {
        new_handle = 0;
        if (zune_is_aborted(dev)) {
            ret = -1;
            zune_set_error("Aborted: %s", filepath);
            break;
        }
        ret = mtp_send_object_info(&dev->ptp, dev->storage_id, folder_id,
                                    &info, &new_handle);
        if (ret == 0) {
            ret = mtp_send_object_from_file(&dev->ptp, send_path,
                                             (uint32_t)fst.st_size);
        }
        if (ret == 0) break;
        fprintf(stderr, "[libzune] photo send attempt %d/3 failed for %s (%s 0x%04X)\n",
                attempt, jpg_name,
                zune_autopsy_name(dev->ptp.last_response),
                dev->ptp.last_response);
        if (new_handle != 0) {
            mtp_delete_object(&dev->ptp, new_handle);
            new_handle = 0;
        }
        if (zune_wound_is_mortal(dev->ptp.last_response))
            break;
        if (attempt < 3) {
            zune_unjam(dev);
            usleep(500000);
        }
    }
    free(info.filename);

    /* Clean up temp file */
    if (prepared) {
        unlink(prepared);
        free(prepared);
    }

    if (ret != 0) {
        fprintf(stderr, "[libzune] send_photo failed after 3 attempts: %s\n", filepath);
        return -1;
    }

    fprintf(stderr, "[libzune] photo sent: %s -> folder %u\n", jpg_name, folder_id);
    return 0;
}

int zune_purge_photo(ZuneDevice *dev, uint32_t item_id) {
    if (!dev) return -1;
    int ret = mtp_delete_object(&dev->ptp, item_id);
    if (ret != 0) {
        fprintf(stderr, "[libzune] delete photo failed: item %u\n", item_id);
        return -1;
    }
    return 0;
}

int zune_extract_photo(ZuneDevice *dev, uint32_t item_id,
                         const char *dest_path) {
    if (!dev || !dest_path) return -1;
    int ret = mtp_get_object_to_file(&dev->ptp, item_id, dest_path);
    if (ret != 0) {
        fprintf(stderr, "[libzune] download photo failed: item %u\n", item_id);
        return -1;
    }
    return 0;
}
