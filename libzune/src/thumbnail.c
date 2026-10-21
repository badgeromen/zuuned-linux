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
    zune_clear_error();
    if (!dev || !jpeg_data || jpeg_len == 0) return ZUNE_BLOWN;

    int ret = mtp_send_representative_sample(&dev->ptp, item_id,
                                             MTP_OFC_JPEG,
                                             0, 0,   /* width/height: let device handle it */
                                             jpeg_data, (uint32_t)jpeg_len);
    if (ret != 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "set_album_art failed for item %u", item_id);
        return ZUNE_BLOWN;
    }
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "album art set for item %u (%zu bytes)",
            item_id, jpeg_len);
    return ZUNE_OK;
}

/* ---- Album art limits ---- */

static uint32_t le(const uint8_t *p, uint32_t size) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < size; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

/* The largest value a property descriptor allows: a UINT32 range maximum
 * (width, height) or a byte-array form's length (sample data). *out is 0
 * when the format declares no limit or lacks the property. Returns -1 on a
 * transport failure or a malformed descriptor. */
static int declared_max(ZuneDevice *dev, uint16_t format, uint16_t prop,
                        uint16_t type, uint32_t *out) {
    uint8_t *d = NULL;
    uint32_t n = 0;
    *out = 0;
    if (mtp_get_prop_desc(&dev->ptp, prop, format, &d, &n) != 0) {
        uint16_t code = dev->ptp.last_response;
        free(d);
        if (code == PTP_RC_MTP_ObjectPropNotSupported || code == PTP_RC_OperationNotSupported ||
            code == PTP_RC_InvalidObjectPropCode || code == PTP_RC_SpecificationByFormatUnsupported)
            return 0;
        zune_set_error("Cannot read artwork property 0x%04X for format 0x%04X (0x%04X)",
                       prop, format, code);
        return -1;
    }
    /* PropertyCode, DataType, GetSet, default value, GroupCode, FormFlag. */
    int ok = d && n >= 5 && le(d, 2) == prop && le(d + 2, 2) == type;
    uint32_t off = 5;
    if (ok && type == PTP_DTC_UINT32) {
        off += 4;
    } else if (ok) {                            /* AUINT8: count, then bytes */
        ok = n - off >= 4 && le(d + off, 4) <= n - off - 4;
        if (ok) off += 4 + le(d + off, 4);
    }
    ok = ok && off <= n && n - off >= 5;                       /* GroupCode, FormFlag */
    if (ok) {
        uint8_t form = d[off + 4];
        off += 5;
        if (type == PTP_DTC_UINT32 && form == 0x01) {          /* Range: min, max, step */
            ok = n - off >= 12;
            if (ok) *out = le(d + off + 4, 4);
        } else if (type != PTP_DTC_UINT32 && form == 0x06) {   /* ByteArray: length */
            ok = n - off >= 4;
            if (ok) *out = le(d + off, 4);
        }
    }
    free(d);
    if (!ok) {
        *out = 0;
        zune_set_error("Malformed artwork property 0x%04X for format 0x%04X", prop, format);
        return -1;
    }
    return 0;
}

int zune_get_brand_limits(ZuneDevice *dev, uint32_t item_id, uint32_t *max_width,
                          uint32_t *max_height, uint32_t *max_bytes) {
    zune_clear_error();
    if (max_width) *max_width = 0;
    if (max_height) *max_height = 0;
    if (max_bytes) *max_bytes = 0;
    if (!dev || !max_width || !max_height || !max_bytes) {
        zune_set_error("zune_get_brand_limits needs a device and three outputs");
        return ZUNE_BLOWN;
    }
    mtp_object_info_t info = {0};
    if (mtp_get_object_info(&dev->ptp, item_id, &info) != 0) {
        mtp_free_object_info(&info);
        zune_set_error("Cannot read item %u for its artwork limits (0x%04X)", item_id,
                       dev->ptp.last_response);
        return ZUNE_BLOWN;
    }
    uint16_t format = info.object_format;
    mtp_free_object_info(&info);

    int cached = -1;
    for (int i = 0; i < dev->brand_limits_count; i++)
        if (dev->brand_limits[i].format == format) cached = i;
    if (cached < 0) {
        uint32_t width = 0, height = 0, bytes = 0;
        if (declared_max(dev, format, MTP_OPC_RepresentativeSampleWidth, PTP_DTC_UINT32, &width) ||
            declared_max(dev, format, MTP_OPC_RepresentativeSampleHeight, PTP_DTC_UINT32, &height) ||
            declared_max(dev, format, MTP_OPC_RepresentativeSampleData, PTP_DTC_AUINT8, &bytes))
            return ZUNE_BLOWN;
        ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "artwork limits for format 0x%04X: %ux%u, %u bytes",
             format, width, height, bytes);
        if (dev->brand_limits_count == (int)(sizeof(dev->brand_limits) / sizeof(dev->brand_limits[0]))) {
            *max_width = width; *max_height = height; *max_bytes = bytes;
            return ZUNE_OK;
        }
        cached = dev->brand_limits_count++;
        dev->brand_limits[cached].format = format;
        dev->brand_limits[cached].width = width;
        dev->brand_limits[cached].height = height;
        dev->brand_limits[cached].bytes = bytes;
    }
    *max_width = dev->brand_limits[cached].width;
    *max_height = dev->brand_limits[cached].height;
    *max_bytes = dev->brand_limits[cached].bytes;
    return ZUNE_OK;
}

/* ---- Thumbnail fetch (no full download) ---- */

int zune_grab_thumb(ZuneDevice *dev, uint32_t item_id,
                        const char *cache_path) {
    zune_clear_error();
    if (!dev || !cache_path) return ZUNE_BLOWN;

    /* Already cached? */
    struct stat st;
    if (stat(cache_path, &st) == 0 && st.st_size > 0)
        return ZUNE_OK;

    /* Get representative sample (uses GetThumb 0x100A) */
    uint8_t *data = NULL;
    uint32_t size = 0;
    int ret = mtp_get_representative_sample(&dev->ptp, item_id, &data, &size);
    if (ret == 0 && data && size > 0) {
        FILE *fp = fopen(cache_path, "wb");
        if (fp) {
            fwrite(data, 1, size, fp);
            fclose(fp);
            ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "got thumbnail for %u (%u bytes)",
                    item_id, size);
            free(data);
            return ZUNE_OK;
        }
    }
    free(data);

    /* No full download fallback — too slow for album art / video posters */
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "all thumbnail methods failed for %u", item_id);
    return ZUNE_BLOWN;
}

/* ---- Photo thumbnail with full fallback chain ---- */

int zune_grab_photo_thumb(ZuneDevice *dev, uint32_t item_id,
                              const char *cache_path) {
    zune_clear_error();
    if (!dev || !cache_path) return ZUNE_BLOWN;

    /* Already cached? */
    struct stat st;
    if (stat(cache_path, &st) == 0 && st.st_size > 0)
        return ZUNE_OK;

    /* Method 1: Representative sample (GetThumb 0x100A) */
    uint8_t *data = NULL;
    uint32_t size = 0;
    int ret = mtp_get_representative_sample(&dev->ptp, item_id, &data, &size);
    if (ret == 0 && data && size > 0) {
        FILE *fp = fopen(cache_path, "wb");
        if (fp) {
            fwrite(data, 1, size, fp);
            fclose(fp);
            ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "got representative sample for %u (%u bytes)",
                    item_id, size);
            free(data);
            return ZUNE_OK;
        }
    }
    free(data);

    /* Method 2: Download full file + resize to 200x200 */
    char label[32];
    snprintf(label, sizeof(label), "photo-%u", item_id);
    char *tmp = zune_mktemp(label, "");
    if (!tmp) return ZUNE_BLOWN;
    ret = mtp_get_object_to_file(&dev->ptp, item_id, tmp);
    if (ret == 0) {
#ifdef __APPLE__
        /* macOS: sips — preserve aspect ratio, fit within 200x200 */
        const char *tool = "sips";
        const char *const argv[] = {
            "sips", "--resampleHeightWidthMax", "200",
            "--setProperty", "format", "jpeg", tmp, "--out", cache_path, NULL
        };
#else
        /* Linux: use ffmpeg */
        const char *tool = "ffmpeg";
        const char *const argv[] = {
            "ffmpeg", "-y", "-i", tmp,
            "-vf", "scale=200:200:force_original_aspect_ratio=decrease",
            cache_path, NULL
        };
#endif
        ret = zune_run_tool(tool, argv, -1);
        remove(tmp);
        free(tmp);

        if (ret == 0 && zune_tool_output_ok(cache_path)) {
            ZLOG(ZUNE_CHATTER_TALK, "libzune", "resized full file for thumbnail %u",
                    item_id);
            return ZUNE_OK;
        }
        /* The cache was absent or empty on entry; a partial file would
         * otherwise be served as "already cached" next time. */
        unlink(cache_path);
    } else {
        remove(tmp);
        free(tmp);
    }

    ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "all photo thumbnail methods failed for %u",
            item_id);
    return ZUNE_BLOWN;
}
