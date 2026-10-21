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
#include "wiretap.h"
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

    /* Photo album folder names read from this device (photo.c); freed by
     * zune_sever. Per device, so another Zune never sees its folder IDs. */
    struct ZuneFolderCacheEntry *folder_cache;

    /* Artwork limits per object format (zune_get_brand_limits). */
    struct { uint16_t format; uint32_t width, height, bytes; } brand_limits[8];
    int brand_limits_count;

    /* Connection state */
    int hd_transport_uncertain;    /* Suppress CloseSession after HD framing loss. */
    int hd_art_active;             /* Reject nested public catalog sessions. */
    int mtpz_authenticated;        /* Only set after a completed handshake */
    /* Set on one thread while a transfer polls them on another: access only
     * through ZUNE_FLAG_GET / ZUNE_FLAG_SET (ptp.h). */
    int disconnecting;
    int cancel_requested;  /* Set by zune_abort() */
};

/* An album, playlist or artist was created but its commit or track
 * references failed: delete the new object, keep zune_autopsy on the original
 * failure, and say in the error whether the cleanup worked. Returns 0
 * (album.c). */
uint32_t zune_forge_failed(ZuneDevice *dev, uint32_t id, const char *kind,
                           const char *name, const char *stage);

/* No answer at all, or the transport failed: the device's state is unknown,
 * so the request may have been carried out. */
static inline int zune_response_uncertain(uint16_t code)
{
    return code == 0 || code == PTP_ERROR_IO;
}

/* ---- One upload engine (upload.c) ----
 *
 * Creates the object (SendObjectInfo from info, or SendObjectPropList from
 * format/props when info is NULL), streams the file, and owns the attempt
 * loop: abort checks, saving the failed response before cleanup, mortal
 * triage, removal of a partial object, unjam and pacing, error reporting. */
typedef struct {
    const char *kind;                 /* "track", "video", ... for logs and errors */
    const char *path;                 /* payload file */
    const char *source;               /* caller's file, named in errors; NULL = path */
    uint32_t size;                    /* bytes declared at creation */
    uint32_t parent;                  /* parent folder; 0 = storage root */
    mtp_object_info_t *info;          /* SendObjectInfo dataset, or NULL */
    uint16_t format;                  /* SendObjectPropList format and properties */
    mtp_prop_entry_t *props;
    int nprops;
    int strict;                       /* never retry after an unanswered creation */
} zune_upload_t;

enum {
    ZUNE_UPLOAD_SENT = 0,             /* *out_handle is the new object */
    ZUNE_UPLOAD_REFUSED = -1,         /* nothing was created (or the source was unusable) */
    ZUNE_UPLOAD_REMOVED = -2,         /* the payload failed and the new object was removed */
    ZUNE_UPLOAD_UNCERTAIN = -3,       /* a creation got no usable answer: an object may exist */
    ZUNE_UPLOAD_ORPHANED = -4         /* the new object could not be removed: *out_handle */
};

/* Run one upload. Returns a ZUNE_UPLOAD_* status; sets the error and keeps
 * zune_autopsy on the failure that ended it. */
int zune_smuggle_core(ZuneDevice *dev, const zune_upload_t *up, uint32_t *out_handle);

/* Free the photo album folder cache (photo.c). */
void zune_free_folder_cache(ZuneDevice *dev);

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

/* Locate an external tool such as "ffmpeg" (util.c): $ZUNE_<NAME> (an
 * executable path, or a name to search for), then PATH, then on macOS only
 * /opt/homebrew/bin and /usr/local/bin. Returns a thread-local path valid
 * until this thread's next call, or NULL with an error naming the tool and
 * its override variable. */
const char *zune_tool_path(const char *name);

/* Create a unique empty file, mode 0600, under $TMPDIR (default /tmp), named
 * zunelinux_<label>_XXXXXX<suffix> (util.c). label may be NULL. Returns a
 * malloc'd path, or NULL with the error set. The caller unlinks and frees it. */
char *zune_mktemp(const char *label, const char *suffix);

/* 1 if a tool produced a nonempty regular file at path (util.c). */
int zune_tool_output_ok(const char *path);

/* Run an external tool with no shell (util.c). argv[0] is the name the tool
 * sees; `tool` is a name resolved by zune_tool_path or an explicit path.
 * Arguments reach the tool byte for byte. stdin/stderr are /dev/null; stdout
 * goes to capture_fd when >= 0, else /dev/null. Returns the exit status, or
 * -1 (error set) if the tool could not start or did not exit normally. */
int zune_run_tool(const char *tool, const char *const argv[], int capture_fd);

/* Fill dev->model and dev->serial from a GetDeviceInfo dataset (devinfo.c).
 * A malformed dataset leaves them unset. */
void zune_parse_device_info(ZuneDevice *dev, const uint8_t *devinfo,
                            uint32_t devinfo_len);

/* Cache folder names from device (photo.c).
 * Call during connect or first photo operation.
 * After this, zune_get_photo_albums() works without USB calls.
 * Returns 0 if the whole folder list was read, -1 if any read failed. */
int zune_cache_folder_names(ZuneDevice *dev);

/* Complete raw ZMDB payload (without the 12-byte transport header). Caller
 * frees output. Internal parser is separately fixture-testable without USB. */
int zune_zmdb_read_payload(zune_usb_handle_t *usb, uint8_t **payload, uint32_t *length);
/* The same read, also returning the 12-byte data container header. */
int zune_zmdb_read_response(zune_usb_handle_t *usb, uint8_t header[12],
                            uint8_t **payload, uint32_t *length);
int zune_parse_podcast_states(const uint8_t *data, size_t length,
                             ZunePodcastState **states, int *count);

/* ---- Thread-local error string ---- */

/*
 * Set the thread-local error message. Supports printf-style formatting.
 * Retrieved by callers via zune_get_error().
 */
void zune_set_error(const char *fmt, ...);
void zune_clear_error(void);

/* The file name of path without its directory or extension (a leading dot
 * is part of the name). Malloc'd; caller frees. NULL when out of memory. */
static inline char *zune_filename_no_ext(const char *path)
{
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

/* 1 if prop is in a GetObjectPropsSupported list. */
static inline int zune_prop_listed(uint16_t prop, const uint16_t *list, int count)
{
    for (int i = 0; i < count; i++)
        if (list[i] == prop)
            return 1;
    return 0;
}

#endif /* ZUNE_INTERNAL_H */
