/* Private, read-only HD artist research. Advertised support and identity
 * candidates are not a background-image upload contract. No tunnel is opened.
 * Limits bound accepted datasets and transactions, not PTP receive allocation. */
#ifndef ZUNETOOL_HD_ARTIST_PROBE_H
#define ZUNETOOL_HD_ARTIST_PROBE_H
#include "zune_internal.h"
#define HD_ART_UINT128 0x000a /* standard PTP UINT128 */

static uint16_t hd_art_u16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}
static uint32_t hd_art_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* A strict MTP string including its count and terminal NUL. */
static int hd_art_string(const uint8_t *p, size_t n, size_t *used) {
    if (!p || !n) return -1;
    size_t count = p[0], bytes = 1 + 2 * count;
    if (bytes > n) return -1;
    if (count) {
        if (hd_art_u16(p + bytes - 2) != 0) return -1;
        for (size_t i = 0; i + 1 < count; ++i) {
            uint16_t c = hd_art_u16(p + 1 + 2*i);
            if (!c) return -1;
            if (c >= 0xd800 && c <= 0xdbff) {
                if (i + 2 >= count) return -1;
                c = hd_art_u16(p + 1 + 2*++i);
                if (c < 0xdc00 || c > 0xdfff) return -1;
            } else if (c >= 0xdc00 && c <= 0xdfff) return -1;
        }
    }
    *used = bytes;
    return 0;
}
static int hd_art_sample_count(const char *arg, unsigned *count) {
    if (!arg || !count || arg[0] < '1' || arg[0] > '8' || arg[1]) return -1;
    *count = (unsigned)(arg[0] - '0');
    return 0;
}
static int hd_art_stopped(ZuneDevice *dev) {
    return ZUNE_FLAG_GET(dev->cancel_requested) || ZUNE_FLAG_GET(dev->disconnecting);
}
static int hd_art_read(ZuneDevice *dev, uint16_t op, uint32_t *params,
                       int count, uint8_t **data, uint32_t *len) {
    *data = NULL; *len = 0;
    if (hd_art_stopped(dev)) return -1;
    uint16_t response = 0;
    return ptp_transaction(&dev->ptp, op, params, count, PTP_DP_GETDATA,
                           NULL, 0, data, len, &response, NULL, NULL);
}
/* Validate the standard GetDeviceInfo fields, printing only operation codes.
 * Pavo returns trailing bytes after SerialNumber. Report their length without
 * interpreting them as capabilities. Identity strings are never emitted. */
static int hd_art_operations(const uint8_t *p, size_t n, FILE *out) {
    size_t pos = 8, used = 0, ops_pos = 0;
    uint32_t ops_count = 0;
    if (!p || n < 8 || n > 65536 || hd_art_string(p+pos, n-pos, &used)) {
        fprintf(out, "device-info-invalid\textension-string\t%zu\n", n); return -1;
    }
    pos += used;
    if (n-pos < 2) return -1;
    pos += 2;
    for (int a = 0; a < 5; ++a) {
        if (n-pos < 4) return -1;
        uint32_t count = hd_art_u32(p+pos); pos += 4;
        if (count > 512 || count > (n-pos)/2) {
            fprintf(out, "device-info-invalid\tarray-count\t%d\t%u\n", a, count); return -1;
        }
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t value = hd_art_u16(p+pos+2*i);
            if (!value) { fprintf(out, "device-info-invalid\tzero-code\t%d\t%u\n", a, i); return -1; }
            for (uint32_t j = 0; j < i; ++j)
                if (value == hd_art_u16(p+pos+2*j)) {
                    fprintf(out, "device-info-invalid\tduplicate-code\t%d\t0x%04X\n", a, value); return -1;
                }
        }
        if (!a) { ops_pos = pos; ops_count = count; }
        pos += count*2;
    }
    for (int i = 0; i < 4; ++i) {
        if (hd_art_string(p+pos, n-pos, &used)) {
            fprintf(out, "device-info-invalid\tidentity-string\t%d\t%zu\n", i, pos); return -1;
        }
        pos += used;
    }
    if (pos != n) fprintf(out, "device-info-uninterpreted-trailing-bytes\t%zu\n", n-pos);
    fprintf(out, "operations\t%u\n", ops_count);
    for (uint32_t i = 0; i < ops_count; ++i)
        fprintf(out, "operation\t0x%04X\n", hd_art_u16(p+ops_pos+2*i));
    return 0;
}

static int hd_artist_probe_report(ZuneDevice *dev, unsigned samples, FILE *out) {
    static const uint16_t candidates[] = {0xdc44, 0xdc07, 0xda99, 0xdc41, 0xda97, 0xdab9};
    uint16_t candidate_types[6] = {0};
    uint8_t *data = NULL;
    uint32_t length = 0, props[256], handles[4096];
    uint32_t prop_count = 0, handle_count = 0;
    const char *failure = "invalid-input";
    int result = -1;
    if (!dev || !out || samples < 1 || samples > 8) return -1;
    fprintf(out, "hd-artist-probe-v1\nscope\tread-only; descriptor headers and identity candidates; image writes unverified\n");
    if (dev->device_family != ZUNE_FAMILY_PAVO) { failure = "not-zune-hd"; goto done; }
    if (!dev->mtpz_authenticated) { failure = "not-authenticated"; goto done; }
    if (hd_art_stopped(dev)) { failure = "cancelled"; goto done; }
    if (hd_art_read(dev, PTP_OC_GetDeviceInfo, NULL, 0, &data, &length)) {
        failure = "device-info-query-failed"; goto done;
    }
    if (hd_art_operations(data, length, out)) { failure = "invalid-device-info"; goto done; }
    free(data); data = NULL;
    uint32_t param[3] = {MTP_OFC_AbstractArtist, 0, 0};
    if (hd_art_read(dev, MTP_OC_GetObjectPropsSupported, param, 1, &data, &length)) {
        failure = "property-list-query-failed"; goto done;
    }
    if (!data || length < 4 || (prop_count = hd_art_u32(data)) > 256 || length != 4+2*prop_count) {
        failure = "invalid-property-list"; goto done;
    }
    for (uint32_t i = 0; i < prop_count; ++i) {
        props[i] = hd_art_u16(data+4+2*i);
        if (!props[i]) { failure = "invalid-property-list"; goto done; }
        for (uint32_t j = 0; j < i; ++j)
            if (props[i] == props[j]) { failure = "invalid-property-list"; goto done; }
    }
    free(data); data = NULL;
    fprintf(out, "artist-properties\t%u\n", prop_count);
    for (uint32_t i = 0; i < prop_count; ++i) {
        if (hd_art_stopped(dev)) { failure = "cancelled"; goto done; }
        if (mtp_get_prop_desc(&dev->ptp, props[i], MTP_OFC_AbstractArtist, &data, &length)) {
            failure = "descriptor-query-failed"; goto done;
        }
        /* Defaults/forms are deliberately not interpreted or certified. */
        if (!data || length < 5 || length > 65536 || hd_art_u16(data) != props[i] || data[4] > 1) {
            failure = "invalid-descriptor-header"; goto done;
        }
        uint16_t type = hd_art_u16(data+2);
        fprintf(out, "descriptor\t0x%04X\t0x%04X\t%s\t%u\n", props[i], type,
                data[4] ? "read-write-advertised" : "read-only-advertised", length);
        for (unsigned c = 0; c < 6; ++c) if (props[i] == candidates[c]) candidate_types[c] = type;
        free(data); data = NULL;
    }
    param[0] = dev->storage_id; param[1] = MTP_OFC_AbstractArtist; param[2] = 0;
    if (hd_art_read(dev, PTP_OC_GetObjectHandles, param, 3, &data, &length)) {
        failure = "inventory-query-failed"; goto done;
    }
    if (!data || length < 4 || (handle_count = hd_art_u32(data)) > 4096 || length != 4+4*handle_count) {
        failure = "invalid-inventory"; goto done;
    }
    for (uint32_t i = 0; i < handle_count; ++i) {
        handles[i] = hd_art_u32(data+4+4*i);
        if (!handles[i] || handles[i] == UINT32_MAX) { failure = "invalid-inventory"; goto done; }
        for (uint32_t j = 0; j < i; ++j)
            if (handles[i] == handles[j]) { failure = "invalid-inventory"; goto done; }
    }
    free(data); data = NULL;
    unsigned sampled = handle_count < samples ? handle_count : samples;
    fprintf(out, "artists-total\t%u\nartists-sampled\t%u\n", handle_count, sampled);
    for (unsigned i = 0; i < sampled; ++i) {
        for (unsigned c = 0; c < 6; ++c) {
            uint16_t type = candidate_types[c];
            if (!type) continue;
            if (type != PTP_DTC_STR && type != HD_ART_UINT128 && type != PTP_DTC_UINT32) {
                fprintf(out, "candidate-skipped\t0x%04X\tunexpected-type-0x%04X\n", candidates[c], type);
                continue;
            }
            if (hd_art_stopped(dev)) { failure = "cancelled"; goto done; }
            if (mtp_get_object_prop_value(&dev->ptp, handles[i], candidates[c], &data, &length)) {
                failure = "identity-query-failed"; goto done;
            }
            size_t used = 0;
            if (!data || (type == PTP_DTC_STR ? (hd_art_string(data, length, &used) || used != length) :
                         length != (type == HD_ART_UINT128 ? 16u : 4u))) {
                failure = "invalid-identity-value"; goto done;
            }
            /* Exact little-endian wire hex prevents terminal/control injection,
             * preserves GUID byte order, and bounds each value to 511 bytes. */
            fprintf(out, "identity-wire-hex\t%u\t0x%04X\t0x%04X\t", handles[i], candidates[c], type);
            for (uint32_t b = 0; b < length; ++b) fprintf(out, "%02x", data[b]);
            fputc('\n', out);
            free(data); data = NULL;
        }
    }
    if (hd_art_stopped(dev)) { failure = "cancelled"; goto done; }
    result = 0;
done:
    free(data);
    if (!result) fprintf(out, "status\tcomplete\n");
    else fprintf(out, "status\t%s\nresponse\t0x%04X\n", failure, dev->ptp.last_response);
    return fflush(out) || ferror(out) ? -1 : result;
}
#endif
