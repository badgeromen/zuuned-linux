/*
 * libzune — devinfo.c
 *
 * GetDeviceInfo (0x1001) dataset parsing, kept apart from the connect path
 * so tests and fuzzers can drive it without USB or authentication.
 */

#include "zune_internal.h"

/* ---- GetDeviceInfo dataset ---- */

/* Cursor helpers: each keeps *off <= len, checking by subtraction so a
 * device-chosen length or count cannot wrap the cursor. */
static int devinfo_skip(size_t len, size_t *off, size_t n)
{
    if (n > len - *off) return 0;
    *off += n;
    return 1;
}

static int devinfo_skip_string(const uint8_t *d, size_t len, size_t *off)
{
    return *off < len && devinfo_skip(len, off, 1 + (size_t)d[*off] * 2);
}

static int devinfo_skip_u16_array(const uint8_t *d, size_t len, size_t *off)
{
    if (len - *off < 4) return 0;
    const uint8_t *p = d + *off;
    size_t count = zune_le32(p);
    *off += 4;
    if (count > (len - *off) / 2) return 0;
    *off += count * 2;
    return 1;
}

/* Fill model and serial from a GetDeviceInfo (0x1001) dataset:
 *   0: uint16 StandardVersion          2: uint32 VendorExtensionID
 *   6: uint16 VendorExtensionVersion   8: MTP String VendorExtensionDesc
 *   then uint16 FunctionalMode, five uint16 arrays (operations, events,
 *   device properties, capture formats, image formats), each a uint32
 *   count + values, then MTP Strings Manufacturer, Model, DeviceVersion,
 *   SerialNumber.
 * A malformed dataset leaves them unset; the caller substitutes defaults. */
void zune_parse_device_info(ZuneDevice *dev, const uint8_t *devinfo,
                            uint32_t devinfo_len)
{
    size_t len = devinfo_len, off = 8;
    int ok = devinfo && len >= off
        && devinfo_skip_string(devinfo, len, &off)        /* VendorExtensionDesc */
        && devinfo_skip(len, &off, 2);                     /* FunctionalMode */
    for (int i = 0; ok && i < 5; i++)
        ok = devinfo_skip_u16_array(devinfo, len, &off);
    ok = ok && devinfo_skip_string(devinfo, len, &off);    /* Manufacturer */
    if (ok && off < len) {
        dev->model = mtp_ucs2_to_string(devinfo + off, (uint32_t)(len - off));
        ok = devinfo_skip_string(devinfo, len, &off);
    }
    ok = ok && devinfo_skip_string(devinfo, len, &off);    /* DeviceVersion */
    if (ok && off < len)
        dev->serial = mtp_ucs2_to_string(devinfo + off, (uint32_t)(len - off));
    if (!ok)
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "GetDeviceInfo: malformed dataset (%u bytes)",
                devinfo_len);
}
