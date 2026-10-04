/*
 * zune_internal.h — Internal header for libzune
 *
 * Shared between library source files. NOT part of the public API.
 * Consumers should only include <zune.h>.
 */

#ifndef ZUNE_INTERNAL_H
#define ZUNE_INTERNAL_H

/* Enable POSIX + BSD extensions for strdup, usleep, etc. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "zune.h"
#include "usb.h"
#include "ptp.h"
#include "mtp.h"
#include "mtpz.h"

/* ---- ZuneDevice struct (opaque to public API) ---- */

/* ZuneDevice typedef — allows 'ZuneDevice *' in .c files without 'struct' keyword */
typedef struct ZuneDevice ZuneDevice;

struct ZuneDevice {
    /* Native PTP/MTP stack (replaces LIBMTP_mtpdevice_t) */
    zune_usb_handle_t usb;
    ptp_session_t     ptp;

    /* Device info */
    char *name;
    char *model;
    char *serial;
    uint16_t usb_pid;             /* USB product ID for model detection */
    uint32_t device_id_raw;       /* Raw MTP property 0xD21A value */
    uint8_t  device_family;       /* Third byte of 0xD21A — ZuneDeviceFamily */
    uint8_t battery;
    uint64_t storage_total;
    uint64_t storage_free;
    uint32_t storage_id;          /* Primary storage ID */
    char *storage_desc;

    /* Cached ZMDB library for duplicate detection (set by zune_infiltrate).
     * Borrowed pointer — caller owns the memory via zune_free_scan(). */
    ZuneDBLibrary *cached_library;

    /* Connection state */
    volatile int disconnecting;
    volatile int cancel_requested;  /* Set by zune_abort() */
};

/* ---- Zune USB vendor ID ---- */

#define ZUNE_VENDOR_ID 0x045e

/* ---- Wound triage for retry loops ----
 *
 * A MORTAL wound is a response code that will not change on retry —
 * the device said "no" on purpose (unsupported op, full store, access
 * denied). Retrying it three times just wastes minutes and hammers a
 * device that already answered. A non-mortal wound (transport error,
 * timeout, busy) is worth clearing the pipes and trying again.
 * Wire captures show the Windows client never retries a refused op. */
static inline int zune_wound_is_mortal(uint16_t rc)
{
    switch (rc) {
    case 0x2005:  /* OperationNotSupported */
    case 0x2006:  /* ParameterNotSupported */
    case 0x2008:  /* InvalidStorageID */
    case 0x2009:  /* InvalidObjectHandle */
    case 0x200A:  /* DevicePropNotSupported */
    case 0x200B:  /* InvalidObjectFormatCode */
    case 0x200C:  /* StoreFull */
    case 0x200D:  /* ObjectWriteProtected */
    case 0x200E:  /* StoreReadOnly */
    case 0x200F:  /* AccessDenied */
    case 0x201D:  /* InvalidParameter */
    case 0xA801:  /* Invalid_ObjectPropCode */
    case 0xA802:  /* Invalid_ObjectProp_Format */
    case 0xA803:  /* Invalid_ObjectProp_Value */
    case 0xA806:  /* Invalid_Dataset */
    case 0xA808:  /* Object_Too_Large */
        return 1;
    default:
        return 0;
    }
}

/* ---- Internal functions shared between source files ---- */

/* Cache folder names from device (photo.c).
 * Call during connect or first photo operation.
 * After this, zune_get_photo_albums() works without USB calls. */
void zune_cache_folder_names(ZuneDevice *dev);

/* ---- Thread-local error string ---- */

/*
 * Set the thread-local error message. Supports printf-style formatting.
 * Retrieved by callers via zune_get_error().
 */
void zune_set_error(const char *fmt, ...);

#endif /* ZUNE_INTERNAL_H */
