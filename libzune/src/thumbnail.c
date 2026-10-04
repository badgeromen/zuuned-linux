/*
 * libzune — thumbnail.c
 * Album art, video poster, and photo thumbnail operations.
 *
 * Uses native PTP/MTP stack (mtp.h) instead of vendored libmtp.
 */

#include "zune_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- Album art (set) ---- */

int zune_brand(ZuneDevice *dev, uint32_t item_id,
                        const uint8_t *jpeg_data, size_t jpeg_len) {
    if (!dev || !jpeg_data || jpeg_len == 0) return -1;

    int ret = mtp_send_representative_sample(&dev->ptp, item_id,
                                             MTP_OFC_JPEG,
                                             0, 0,   /* width/height: let device handle it */
                                             jpeg_data, (uint32_t)jpeg_len);
    if (ret != 0) {
        fprintf(stderr, "[libzune] set_album_art failed for item %u\n", item_id);
        return -1;
    }
    fprintf(stderr, "[libzune] album art set for item %u (%zu bytes)\n",
            item_id, jpeg_len);
    return 0;
}

/* ---- Thumbnail fetch (no full download) ---- */

int zune_grab_thumb(ZuneDevice *dev, uint32_t item_id,
                        const char *cache_path) {
    if (!dev || !cache_path) return -1;

    /* Already cached? */
    struct stat st;
    if (stat(cache_path, &st) == 0 && st.st_size > 0)
        return 0;

    /* Get representative sample (uses GetThumb 0x100A) */
    uint8_t *data = NULL;
    uint32_t size = 0;
    int ret = mtp_get_representative_sample(&dev->ptp, item_id, &data, &size);
    if (ret == 0 && data && size > 0) {
        FILE *fp = fopen(cache_path, "wb");
        if (fp) {
            fwrite(data, 1, size, fp);
            fclose(fp);
            fprintf(stderr, "[libzune] got thumbnail for %u (%u bytes)\n",
                    item_id, size);
            free(data);
            return 0;
        }
    }
    free(data);

    /* No full download fallback — too slow for album art / video posters */
    fprintf(stderr, "[libzune] all thumbnail methods failed for %u\n", item_id);
    return -1;
}

/* ---- Photo thumbnail with full fallback chain ---- */

int zune_grab_photo_thumb(ZuneDevice *dev, uint32_t item_id,
                              const char *cache_path) {
    if (!dev || !cache_path) return -1;

    /* Already cached? */
    struct stat st;
    if (stat(cache_path, &st) == 0 && st.st_size > 0)
        return 0;

    /* Method 1: Representative sample (GetThumb 0x100A) */
    uint8_t *data = NULL;
    uint32_t size = 0;
    int ret = mtp_get_representative_sample(&dev->ptp, item_id, &data, &size);
    if (ret == 0 && data && size > 0) {
        FILE *fp = fopen(cache_path, "wb");
        if (fp) {
            fwrite(data, 1, size, fp);
            fclose(fp);
            fprintf(stderr, "[libzune] got representative sample for %u (%u bytes)\n",
                    item_id, size);
            free(data);
            return 0;
        }
    }
    free(data);

    /* Method 2: Download full file + resize to 200x200 */
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "/tmp/zunelinux-photo-%u", item_id);
    ret = mtp_get_object_to_file(&dev->ptp, item_id, tmp);
    if (ret == 0) {
        char cmd[1024];
#ifdef __APPLE__
        /* macOS: sips — preserve aspect ratio, fit within 200x200 */
        snprintf(cmd, sizeof(cmd),
                 "sips --resampleHeightWidthMax 200 --setProperty format jpeg '%s' --out '%s' 2>/dev/null",
                 tmp, cache_path);
#else
        /* Linux: use ffmpeg */
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -i '%s' -vf "
                 "scale=200:200:force_original_aspect_ratio=decrease "
                 "'%s' 2>/dev/null",
                 tmp, cache_path);
#endif
        ret = system(cmd);
        remove(tmp);

        if (ret == 0) {
            fprintf(stderr, "[libzune] resized full file for thumbnail %u\n",
                    item_id);
            return 0;
        }
    } else {
        remove(tmp);
    }

    fprintf(stderr, "[libzune] all photo thumbnail methods failed for %u\n",
            item_id);
    return -1;
}
