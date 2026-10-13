/* Private research command. Query format descriptors, never individual media
 * or setters. Advertised properties are evidence, not proof that writes work.
 * Standalone header keeps the command testable against the real MTP decoder. */
#ifndef ZUNETOOL_MEDIA_CAPABILITIES_H
#define ZUNETOOL_MEDIA_CAPABILITIES_H

#include "zune_internal.h"
#include <errno.h>

static int media_probe_format(const char *text, uint16_t *format)
{
    if (!text || !*text || !format) return -1;
    /* Reject signs/whitespace before strtoul, and reject trailing text. */
    if (*text < '0' || *text > '9') return -1;
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno || *end || value == 0 || value > 0xffff) return -1;
    *format = (uint16_t)value;
    return 0;
}

static int media_probe_report(ZuneDevice *dev, uint16_t format, FILE *output)
{
    if (!dev || !output || !format) return -1;
    fprintf(output, "media-capabilities-v1\nformat\t0x%04X\n", format);
    fprintf(output, "scope\tadvertised descriptor headers; writes unverified\n");
    if (dev->cancel_requested || dev->disconnecting) {
        fprintf(output, "status\tcancelled\n");
        return -1;
    }
    uint16_t *properties = NULL;
    int count = 0;
    if (mtp_get_props_supported(&dev->ptp, format, &properties, &count) != 0) {
        fprintf(output, "status\tquery-failed\nresponse\t0x%04X\n", dev->ptp.last_response);
        free(properties);
        return -1;
    }
    /* Limit transactions even for a corrupt/hostile supported-property list. */
    if (count < 0 || count > 256 || (count && !properties)) {
        fprintf(output, "status\tproperty-limit-or-invalid-list\n");
        free(properties);
        return -1;
    }
    fprintf(output, "properties\t%d\nproperty\ttype\tget-set\tdescriptor-bytes\n", count);
    int result = 0;
    for (int i = 0; i < count; ++i) {
        if (dev->cancel_requested || dev->disconnecting) {
            fprintf(output, "status\tcancelled\n");
            result = -1;
            break;
        }
        uint8_t *data = NULL;
        uint32_t length = 0;
        int status = mtp_get_prop_desc(&dev->ptp, properties[i], format, &data, &length);
        if (status != 0) {
            fprintf(output, "status\tdescriptor-query-failed\nproperty\t0x%04X\nresponse\t0x%04X\n",
                    properties[i], dev->ptp.last_response);
            free(data);
            result = -1;
            break;
        }
        /* Only the common header is interpreted. Defaults, group codes and
         * forms remain deliberately unparsed; do not infer usable write ranges. */
        if (!data || length < 5 || length > 65536 ||
            ((uint16_t)data[0] | ((uint16_t)data[1] << 8)) != properties[i] || data[4] > 1) {
            fprintf(output, "status\tinvalid-descriptor-header\nproperty\t0x%04X\n", properties[i]);
            free(data);
            result = -1;
            break;
        }
        uint16_t type = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
        fprintf(output, "0x%04X\t0x%04X\t%s\t%u\n", properties[i], type,
                data[4] ? "read-write-advertised" : "read-only-advertised", length);
        free(data);
    }
    free(properties);
    if (result == 0) fprintf(output, "status\tcomplete\n");
    return fflush(output) != 0 || ferror(output) ? -1 : result;
}
#endif
