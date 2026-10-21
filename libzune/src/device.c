/*
 * device.c — Device connection, lifecycle, and info getters
 *
 * Part of libzune. Uses native PTP/MTP/MTPZ stack (no libmtp).
 */

#include "zune_internal.h"
#include <gcrypt.h>

/* ---- Thread-local error storage ---- */

static __thread char tls_error[1024] = {0};

void zune_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tls_error, sizeof(tls_error), fmt, ap);
    va_end(ap);
}

/* Every public call that can fail starts here, so zune_get_error describes
 * the most recent such call (T3.5). */
void zune_clear_error(void) {
    tls_error[0] = 0;
}

const char *zune_get_error(void) {
    return tls_error[0] ? tls_error : NULL;
}

/* ---- Runtime version ---- */

#define ZUNE_STRINGIFY_(x) #x
#define ZUNE_STRINGIFY(x) ZUNE_STRINGIFY_(x)

const char *zune_dossier(void) {
    return ZUNE_STRINGIFY(LIBZUNE_VERSION_MAJOR) "."
           ZUNE_STRINGIFY(LIBZUNE_VERSION_MINOR) "."
           ZUNE_STRINGIFY(LIBZUNE_VERSION_PATCH);
}

/* ---- Connect ---- */

/* Allocate the device and open its USB interface, retrying for an
 * interface the host has not released yet. NULL on failure. */
static ZuneDevice *breach_open(void) {
    /* Find and open the Zune USB device */
    ZuneDevice *dev = calloc(1, sizeof(ZuneDevice));
    if (!dev) {
        zune_set_error("Out of memory allocating ZuneDevice");
        return NULL;
    }

    /* Retry loop — macOS may need time to release USB interfaces after
     * killing daemons. Up to 3 attempts with 2s between retries. */
    int connected = 0;
    for (int attempt = 1; attempt <= 3; attempt++) {
        memset(&dev->usb, 0, sizeof(dev->usb));

        if (zune_usb_find_device(&dev->usb) == ZUNE_USB_OK) {
            connected = 1;
            break;
        }

        if (attempt < 3) {
            ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "USB open attempt %d failed, retrying...", attempt);
            usleep(2000000);
        }
    }

    if (!connected) {
        zune_set_error("No Zune device found. Is your Zune connected?");
        free(dev);
        return NULL;
    }

    return dev;
}

/* Open the PTP session, resetting the device once if it does not answer.
 * -1 on failure, with the USB interface closed. */
static int breach_session(ZuneDevice *dev) {
    /* Initialize PTP session */
    ptp_session_init(&dev->ptp, &dev->usb);
    /* Let the protocol layer see zune_abort() so a cancel can stop a
     * transfer between chunks instead of after the whole file. */
    dev->ptp.abort_flag = &dev->cancel_requested;

    if (ptp_open_session(&dev->ptp) < 0) {
        /* Zune 30 (Keel) quirk, observed on Linux/libusb: after two
         * breach/sever cycles the device stops answering OpenSession
         * entirely (bulk IN times out before any data). A USB device
         * reset revives it every time. Reset, reopen, retry once. */
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "OpenSession dead — resetting device and retrying");
        dev->usb.backend->reset(&dev->usb);
        dev->usb.backend->close(&dev->usb);
        usleep(1500000);

        memset(&dev->usb, 0, sizeof(dev->usb));
        if (zune_usb_find_device(&dev->usb) != ZUNE_USB_OK) {
            zune_set_error("Zune did not come back after USB reset");
            return -1;
        }
        ptp_session_init(&dev->ptp, &dev->usb);
        dev->ptp.abort_flag = &dev->cancel_requested;

        if (ptp_open_session(&dev->ptp) < 0) {
            zune_set_error("Failed to open PTP session (even after device reset)");
            dev->usb.backend->close(&dev->usb);
            return -1;
        }
    }

    return 0;
}

/* The first GetDeviceInfo, then MTPZ authentication. A failed handshake
 * leaves the device usable for reads. */
static void breach_auth(ZuneDevice *dev) {
    /* GetDeviceInfo, call 1 of 2: MUST run before MTPZ — it initializes MTP
     * session state. Vendored libmtp does this; without it, MTPZ operations
     * return 0x2002. Its dataset is discarded on purpose. Keep both calls:
     * this one is a protocol prerequisite, the second is the one parsed. */
    {
        uint8_t *devinfo = NULL;
        uint32_t devinfo_len = 0;
        uint16_t resp = 0;
        ptp_transaction(&dev->ptp, PTP_OC_GetDeviceInfo,
                        NULL, 0, PTP_DP_GETDATA,
                        NULL, 0, &devinfo, &devinfo_len,
                        &resp, NULL, NULL);
        /* We'll parse devinfo later — just need the call to happen first */
        if (devinfo) {
            /* Quick parse for model/serial while we have the data */
            /* (full parsing happens below) */
            free(devinfo);
        }
        ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "GetDeviceInfo sent (%u bytes received)", devinfo_len);
    }

    /* MTPZ authentication — AFTER GetDeviceInfo */
    if (mtpz_keys_available()) {
        if (mtpz_handshake(&dev->ptp) < 0) {
            ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "MTPZ handshake failed — device may reject operations");
            /* Don't abort — some operations may still work, and the user
             * should see the error in the UI rather than a silent failure */
        } else {
            dev->mtpz_authenticated = 1;
            ZLOG(ZUNE_CHATTER_TALK, "libzune", "MTPZ authentication successful");
        }
    }
}

/* Model, serial, friendly name, device family and battery level. */
static void breach_identify(ZuneDevice *dev) {
    /* GetDeviceInfo, call 2 of 2 (0x1001), after authentication: the dataset
     * parsed for model, serial, vendor, etc. It works without MTPZ, but
     * individual GetDevicePropValue calls fail without authentication. */
    {
        uint8_t *devinfo = NULL;
        uint32_t devinfo_len = 0;
        uint16_t resp = 0;

        int ret2 = ptp_transaction(&dev->ptp, PTP_OC_GetDeviceInfo,
                                    NULL, 0, PTP_DP_GETDATA,
                                    NULL, 0, &devinfo, &devinfo_len,
                                    &resp, NULL, NULL);

        if (ret2 == 0 && devinfo && devinfo_len > 0) {
            zune_parse_device_info(dev, devinfo, devinfo_len);
            free(devinfo);
            ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "GetDeviceInfo: model='%s' serial='%s'",
                    dev->model ? dev->model : "(null)",
                    dev->serial ? dev->serial : "(null)");
        }

        if (!dev->model) dev->model = strdup("Unknown");
        if (!dev->serial) dev->serial = strdup("");
    }

    /* Try friendly name via GetDevicePropValue — may fail without MTPZ */
    {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (ptp_get_device_prop_value(&dev->ptp, 0xD402, &data, &len) == 0 && data) {
            dev->name = mtp_ucs2_to_string(data, len);
            free(data);
        }
        if (!dev->name) dev->name = strdup("Zune");
    }

    /* Device family identification (MTP property 0xD21A) */
    {
        uint8_t *data = NULL;
        uint32_t len = 0;
        dev->device_family = ZUNE_FAMILY_UNKNOWN;
        if (ptp_get_device_prop_value(&dev->ptp, 0xD21A, &data, &len) == 0 && data && len >= 4) {
            dev->device_id_raw = zune_le32(data);
            dev->device_family = (dev->device_id_raw >> 24) & 0xFF;
            const char *family_name = "unknown";
            switch (dev->device_family) {
            case 0x00: family_name = "Keel (Zune 30)"; break;
            case 0x02: family_name = "Scorpius (Flash 4/8/16)"; break;
            case 0x03: family_name = "Draco (HDD 80/120)"; break;
            case 0x06: family_name = "Pavo (Zune HD)"; break;
            }
            ZLOG(ZUNE_CHATTER_TALK, "libzune", "device family: %s (0xD21A = 0x%08X)",
                    family_name, dev->device_id_raw);
        } else {
            ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "device property 0xD21A not available, family unknown");
        }
        free(data);
    }

    /* Battery level (device property 0x5001) */
    {
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (ptp_get_device_prop_value(&dev->ptp, 0x5001, &data, &len) == 0 && data && len >= 1) {
            dev->battery = data[0];
            free(data);
        }
    }
}

/* The first storage's ID, capacity and free space. */
static void breach_storage(ZuneDevice *dev) {
    /* Storage info */
    {
        uint32_t *storage_ids = NULL;
        int nstorage = 0;
        if (mtp_get_storage_ids(&dev->ptp, &storage_ids, &nstorage) == 0 && nstorage > 0) {
            dev->storage_id = storage_ids[0];

            mtp_storage_info_t sinfo;
            memset(&sinfo, 0, sizeof(sinfo));
            if (mtp_get_storage_info(&dev->ptp, dev->storage_id, &sinfo) == 0) {
                dev->storage_total = sinfo.max_capacity;
                dev->storage_free = sinfo.free_space;
                dev->storage_desc = sinfo.storage_description
                    ? strdup(sinfo.storage_description)
                    : strdup("Storage");
                mtp_free_storage_info(&sinfo);
            }
            free(storage_ids);
        }
    }
}

ZuneDevice *zune_breach(void) {
    zune_clear_error();
    /* libgcrypt is required for MTPZ auth. A host that set it up keeps its
     * own configuration. */
    if (mtpz_init_gcrypt() < 0) {
        zune_set_error("libgcrypt %s or newer is required (found %s)", GCRYPT_VERSION,
                       gcry_check_version(NULL));
        return NULL;
    }

    /* MTPZ keys: loaded on the first breach, kept until zune_go_dark. */
    if (mtpz_load_keys() < 0) {
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "warning: MTPZ keys not loaded from ~/.mtpz-data");
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "MTPZ authentication will fail");
    }

    ZuneDevice *dev = breach_open();
    if (!dev)
        return NULL;

    ZUNE_FLAG_SET(dev->disconnecting, 0);

    if (breach_session(dev) < 0) {
        free(dev);
        return NULL;
    }
    breach_auth(dev);
    breach_identify(dev);
    breach_storage(dev);

    ZLOG(ZUNE_CHATTER_TALK, "libzune", "connected: %s (%s), battery: %u%%, storage: %llu/%llu",
            dev->name, dev->model, dev->battery,
            (unsigned long long)dev->storage_free,
            (unsigned long long)dev->storage_total);

    return dev;
}

/* ---- Disconnect ---- */

void zune_sever(ZuneDevice *dev) {
    if (!dev) return;

    ZUNE_FLAG_SET(dev->disconnecting, 1);
    dev->cached_library = NULL;  /* borrowed ref — don't free, caller owns it */
    usleep(300000);

    if (!dev->hd_transport_uncertain)
        ptp_close_session(&dev->ptp);

    if (dev->usb.backend)
        dev->usb.backend->close(&dev->usb);

    zune_free_folder_cache(dev);
    free(dev->name);
    free(dev->model);
    free(dev->serial);
    free(dev->storage_desc);
    free(dev);
}

void zune_go_dark(void) {
    mtpz_free_keys();
}

/* ---- Info getters ---- */

const char *zune_get_name(ZuneDevice *dev) {
    return dev ? dev->name : NULL;
}

const char *zune_get_model(ZuneDevice *dev) {
    return dev ? dev->model : NULL;
}

const char *zune_get_serial(ZuneDevice *dev) {
    return dev ? dev->serial : NULL;
}

uint8_t zune_get_battery(ZuneDevice *dev) {
    return dev ? dev->battery : 0;
}

uint64_t zune_get_capacity(ZuneDevice *dev) {
    return dev ? dev->storage_total : 0;
}

uint64_t zune_get_headroom(ZuneDevice *dev) {
    return dev ? dev->storage_free : 0;
}

/* Re-read GetStorageInfo from the device and update the cached free/total.
 * zune_get_headroom() only reflects the connect-time snapshot; call this
 * during a sync to get the LIVE free space before committing each file so
 * we don't overcommit and hit StoreFull (0x200C). Cheap (~1 GetStorageInfo
 * transaction). Returns 0 on success, -1 on failure (cached values kept). */
int zune_refresh_storage(ZuneDevice *dev) {
    zune_clear_error();
    if (!dev || dev->storage_id == 0) return ZUNE_BLOWN;
    mtp_storage_info_t sinfo;
    memset(&sinfo, 0, sizeof(sinfo));
    if (mtp_get_storage_info(&dev->ptp, dev->storage_id, &sinfo) != 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "refresh_storage: GetStorageInfo failed (%s 0x%04X)",
                zune_autopsy_name(dev->ptp.last_response), dev->ptp.last_response);
        return ZUNE_BLOWN;
    }
    dev->storage_total = sinfo.max_capacity;
    dev->storage_free  = sinfo.free_space;
    mtp_free_storage_info(&sinfo);
    return ZUNE_OK;
}

/* ---- Model detection ---- */

ZuneModel zune_identify(ZuneDevice *dev) {
    if (!dev) return ZUNE_MODEL_UNKNOWN;

    /* Primary: use device family from MTP property 0xD21A */
    switch (dev->device_family) {
    case ZUNE_FAMILY_KEEL:     return ZUNE_MODEL_30;
    case ZUNE_FAMILY_SCORPIUS: return ZUNE_MODEL_80;
    case ZUNE_FAMILY_DRACO:    return ZUNE_MODEL_80;
    case ZUNE_FAMILY_PAVO:     return ZUNE_MODEL_HD;
    default: break;
    }

    /* Fallback: string matching on model name */
    if (dev->model) {
        if (strstr(dev->model, "Zune HD") || strstr(dev->model, "ZuneHD"))
            return ZUNE_MODEL_HD;
        if (strstr(dev->model, "Zune 30") || strstr(dev->model, "Zune30"))
            return ZUNE_MODEL_30;
        if (strstr(dev->model, "Zune 80") || strstr(dev->model, "Zune80") ||
            strstr(dev->model, "Zune 120") || strstr(dev->model, "Zune120") ||
            strstr(dev->model, "Zune 4") || strstr(dev->model, "Zune4") ||
            strstr(dev->model, "Zune 8") || strstr(dev->model, "Zune8") ||
            strstr(dev->model, "Zune 16") || strstr(dev->model, "Zune16"))
            return ZUNE_MODEL_80;
    }

    ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "model detection failed — family=0x%02X model='%s'",
            dev->device_family, dev->model ? dev->model : "(null)");
    return ZUNE_MODEL_UNKNOWN;
}

ZuneDeviceFamily zune_get_family(ZuneDevice *dev) {
    if (!dev) return ZUNE_FAMILY_UNKNOWN;
    switch (dev->device_family) {
    case 0x00: return ZUNE_FAMILY_KEEL;
    case 0x02: return ZUNE_FAMILY_SCORPIUS;
    case 0x03: return ZUNE_FAMILY_DRACO;
    case 0x06: return ZUNE_FAMILY_PAVO;
    default:   return ZUNE_FAMILY_UNKNOWN;
    }
}

int zune_is_hdd(ZuneDevice *dev) {
    if (!dev) return 0;
    return dev->device_family == ZUNE_FAMILY_KEEL ||
           dev->device_family == ZUNE_FAMILY_DRACO;
}

/* ---- Connection check ---- */

int zune_is_live(ZuneDevice *dev) {
    if (!dev) return 0;
    if (ZUNE_FLAG_GET(dev->disconnecting)) return 0;
    if (!dev->usb.backend) return 0;
    return 1;
}

/* ---- Folder operations ---- */

uint32_t zune_forge_folder(ZuneDevice *dev, const char *name, uint32_t parent_id) {
    zune_clear_error();
    if (!dev || !name) return 0;
    uint32_t new_handle = 0;
    if (mtp_create_folder(&dev->ptp, parent_id, dev->storage_id, name, &new_handle) < 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "create folder '%s' failed", name);
        return 0;
    }
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "created folder: %s (id=%u)", name, new_handle);
    return new_handle;
}

int zune_get_folders(ZuneDevice *dev, ZuneFolderEntry **out) {
    zune_clear_error();
    if (out) *out = NULL;
    if (!dev || !out) {
        zune_set_error("Invalid device or folder output");
        return ZUNE_BLOWN;
    }

    /* Get all folder handles (format=Association, parent=root). A failed
     * query is an error, not an empty device. */
    uint32_t *handles = NULL;
    int nhandles = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_Association,
                                0x00000000, &handles, &nhandles) < 0) {
        free(handles);
        zune_set_error("Could not list device folders (%s 0x%04X)",
                       zune_autopsy_name(dev->ptp.last_response), dev->ptp.last_response);
        return ZUNE_BLOWN;
    }
    if (nhandles == 0) {
        free(handles);
        return 0;
    }

    ZuneFolderEntry *entries = calloc(nhandles, sizeof(ZuneFolderEntry));
    if (!entries) {
        free(handles);
        zune_set_error("Out of memory listing %d folders", nhandles);
        return ZUNE_BLOWN;
    }

    int count = 0;
    int fail_streak = 0;
    for (int i = 0; i < nhandles; i++) {
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, handles[i], &info) == 0) {
            entries[count].id = handles[i];
            entries[count].name = info.filename ? strdup(info.filename) : strdup("Unknown");
            count++;
            mtp_free_object_info(&info);
            fail_streak = 0;
        } else if (++fail_streak >= 2) {
            /* A dead transport burns a full USB timeout per call: stop, and
             * report it instead of returning part of the list as complete. */
            uint16_t code = dev->ptp.last_response;
            ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "get_folders: transport looks dead, "
                    "stopping after %d/%d", i + 1, nhandles);
            zune_free_folders(entries, count);
            free(handles);
            zune_set_error("Could not read device folders: no answer after %d of %d (%s 0x%04X)",
                           i + 1, nhandles, zune_autopsy_name(code), code);
            return ZUNE_BLOWN;
        }
    }

    free(handles);
    *out = entries;
    return count;
}

void zune_free_folders(ZuneFolderEntry *entries, int count) {
    if (!entries) return;
    for (int i = 0; i < count; i++) free(entries[i].name);
    free(entries);
}

/* ---- Cancel support ---- */

void zune_abort(ZuneDevice *dev) {
    if (dev) ZUNE_FLAG_SET(dev->cancel_requested, 1);
}

int zune_is_aborted(ZuneDevice *dev) {
    return dev ? ZUNE_FLAG_GET(dev->cancel_requested) : 0;
}

void zune_clear_abort(ZuneDevice *dev) {
    if (dev) ZUNE_FLAG_SET(dev->cancel_requested, 0);
}

/* ---- Autopsy: cause of death for the last operation ---- */

uint16_t zune_autopsy(ZuneDevice *dev) {
    return dev ? dev->ptp.last_response : 0;
}

const char *zune_autopsy_name(uint16_t code) {
    switch (code) {
    case 0x0000: return "NoResponse";
    case 0x2001: return "OK";
    case 0x2002: return "GeneralError";
    case 0x2003: return "SessionNotOpen";
    case 0x2005: return "OperationNotSupported";
    case 0x2006: return "ParameterNotSupported";
    case 0x2007: return "IncompleteTransfer";
    case 0x2008: return "InvalidStorageID";
    case 0x2009: return "InvalidObjectHandle";
    case 0x200A: return "DevicePropNotSupported";
    case 0x200B: return "InvalidObjectFormatCode";
    case 0x200C: return "StoreFull";
    case 0x200D: return "ObjectWriteProtected";
    case 0x200E: return "StoreReadOnly";
    case 0x200F: return "AccessDenied";
    case 0x2013: return "StoreNotAvailable";
    case 0x2019: return "DeviceBusy";
    case 0x201D: return "InvalidParameter";
    case 0x201E: return "SessionAlreadyOpen";
    case 0x201F: return "TransactionCancelled";
    case 0xA801: return "MTP_InvalidObjectPropCode";
    case 0xA802: return "MTP_InvalidObjectPropFormat";
    case 0xA803: return "MTP_InvalidObjectPropValue";
    case 0xA804: return "MTP_InvalidObjectReference";
    case 0xA806: return "MTP_InvalidDataset";
    case 0xA808: return "MTP_ObjectTooLarge";
    case 0x02FF: return "TransportError";
    default:     return "Unknown";
    }
}

/* ---- Rename device ---- */

int zune_rename(ZuneDevice *dev, const char *new_name) {
    zune_clear_error();
    if (!dev || !new_name) return ZUNE_BLOWN;

    /* Encode as MTP string (UCS-2LE wire format) */
    uint32_t wire_len = 0;
    uint8_t *wire = mtp_string_to_ucs2(new_name, &wire_len);
    if (!wire || wire_len == 0) {
        zune_set_error("Failed to encode device name to UCS-2");
        free(wire);
        return ZUNE_BLOWN;
    }

    /* Set Friendly Name device property (0xD402) */
    int ret = ptp_set_device_prop_value(&dev->ptp, 0xD402, wire, wire_len);
    free(wire);

    if (ret == 0) {
        free(dev->name);
        dev->name = strdup(new_name);
        ZLOG(ZUNE_CHATTER_TALK, "libzune", "device renamed to '%s'", new_name);
    } else {
        zune_set_error("Failed to set device name property (0xD402)");
    }

    return ret;
}
