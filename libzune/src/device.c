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

const char *zune_get_error(void) {
    return tls_error[0] ? tls_error : NULL;
}

/* ---- Kill competing MTP daemons ---- */
/* COMMENTED OUT: Replaced by DEXT (DriverKit driver extension).
 * Kept for reference only. Removed from public API (zune.h).

void zune_kill_competitors_fast(void) {
#ifdef __APPLE__
    system("killall -9 PTPCamera 2>/dev/null");
    system("killall -9 ptpcamerad 2>/dev/null");
    system("killall -9 AMPDeviceDiscoveryAgent 2>/dev/null");
    system("killall -9 AMPLibraryAgent 2>/dev/null");
#endif
}

void zune_kill_competitors(void) {
#ifdef __APPLE__
    zune_kill_competitors_fast();
    usleep(3000000);
#else
    system("systemctl --user stop gvfs-mtp-volume-monitor.service 2>/dev/null");
    system("systemctl --user mask gvfs-mtp-volume-monitor.service 2>/dev/null");
    system("pkill -9 -f gvfsd-mtp 2>/dev/null");
    system("pkill -9 -f gvfs-mtp 2>/dev/null");
    usleep(1500000);
#endif
}
*/

/* ---- Connect ---- */

ZuneDevice *zune_breach(void) {
    /* Initialize libgcrypt before anything — required for MTPZ auth. */
    gcry_check_version(NULL);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

    /* Load MTPZ keys from ~/.mtpz-data */
    if (mtpz_load_keys() < 0) {
        fprintf(stderr, "[libzune] warning: MTPZ keys not loaded from ~/.mtpz-data\n");
        fprintf(stderr, "[libzune] MTPZ authentication will fail\n");
    }

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
            fprintf(stderr, "[libzune] USB open attempt %d failed, retrying...\n", attempt);
            usleep(2000000);
        }
    }

    if (!connected) {
        zune_set_error("No Zune device found. Is your Zune connected?");
        free(dev);
        return NULL;
    }

    dev->disconnecting = 0;

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
        fprintf(stderr, "[libzune] OpenSession dead — resetting device and retrying\n");
        dev->usb.backend->reset(&dev->usb);
        dev->usb.backend->close(&dev->usb);
        usleep(1500000);

        memset(&dev->usb, 0, sizeof(dev->usb));
        if (zune_usb_find_device(&dev->usb) != ZUNE_USB_OK) {
            zune_set_error("Zune did not come back after USB reset");
            free(dev);
            return NULL;
        }
        ptp_session_init(&dev->ptp, &dev->usb);
        dev->ptp.abort_flag = &dev->cancel_requested;

        if (ptp_open_session(&dev->ptp) < 0) {
            zune_set_error("Failed to open PTP session (even after device reset)");
            dev->usb.backend->close(&dev->usb);
            free(dev);
            return NULL;
        }
    }

    /* GetDeviceInfo MUST be called before MTPZ — initializes MTP session state.
     * Vendored libmtp does this; without it, MTPZ operations return 0x2002. */
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
        fprintf(stderr, "[libzune] GetDeviceInfo sent (%u bytes received)\n", devinfo_len);
    }

    /* MTPZ authentication — AFTER GetDeviceInfo */
    if (mtpz_keys_available()) {
        if (mtpz_handshake(&dev->ptp) < 0) {
            fprintf(stderr, "[libzune] MTPZ handshake failed — device may reject operations\n");
            /* Don't abort — some operations may still work, and the user
             * should see the error in the UI rather than a silent failure */
        } else {
            fprintf(stderr, "[libzune] MTPZ authentication successful\n");
        }
    }

    /* Read device info via PTP GetDeviceInfo (0x1001) — works without MTPZ.
     * This returns a dataset with model, serial, vendor, etc.
     * Individual GetDevicePropValue calls fail without MTPZ authentication. */
    {
        uint8_t *devinfo = NULL;
        uint32_t devinfo_len = 0;
        uint16_t resp = 0;

        int ret2 = ptp_transaction(&dev->ptp, PTP_OC_GetDeviceInfo,
                                    NULL, 0, PTP_DP_GETDATA,
                                    NULL, 0, &devinfo, &devinfo_len,
                                    &resp, NULL, NULL);

        if (ret2 == 0 && devinfo && devinfo_len > 0) {
            /* Parse GetDeviceInfo dataset:
             * Offset  0: uint16 StandardVersion
             * Offset  2: uint32 VendorExtensionID
             * Offset  6: uint16 VendorExtensionVersion
             * Offset  8: MTP String VendorExtensionDesc
             * Then: uint16 FunctionalMode
             * Then: uint32 array OperationsSupported
             * Then: uint32 array EventsSupported
             * Then: uint32 array DevicePropertiesSupported
             * Then: uint32 array CaptureFormats
             * Then: uint32 array ImageFormats
             * Then: MTP String Manufacturer
             * Then: MTP String Model
             * Then: MTP String DeviceVersion
             * Then: MTP String SerialNumber
             */
            uint32_t off = 8;  /* skip to VendorExtensionDesc */

            /* Skip VendorExtensionDesc (MTP string) */
            if (off < devinfo_len) {
                uint8_t slen = devinfo[off];
                off += 1 + slen * 2;
            }

            /* Skip FunctionalMode (uint16) */
            off += 2;

            /* Skip arrays: each is uint32 count + count * uint16/uint32 */
            for (int skip = 0; skip < 5 && off + 4 <= devinfo_len; skip++) {
                uint32_t count = devinfo[off] | (devinfo[off+1]<<8) |
                                 (devinfo[off+2]<<16) | (devinfo[off+3]<<24);
                off += 4;
                if (skip < 4)
                    off += count * 2; /* uint16 arrays (ops, events, props, capture formats) */
                else
                    off += count * 2; /* uint16 array (image formats) */
            }

            /* Now we should be at: Manufacturer, Model, DeviceVersion, SerialNumber */
            /* Read Manufacturer (skip it) */
            if (off < devinfo_len) {
                uint8_t slen = devinfo[off];
                off += 1 + slen * 2;
            }

            /* Read Model */
            if (off < devinfo_len) {
                dev->model = mtp_ucs2_to_string(devinfo + off, devinfo_len - off);
                uint8_t slen = devinfo[off];
                off += 1 + slen * 2;
            }

            /* Read DeviceVersion (skip) */
            if (off < devinfo_len) {
                uint8_t slen = devinfo[off];
                off += 1 + slen * 2;
            }

            /* Read SerialNumber */
            if (off < devinfo_len) {
                dev->serial = mtp_ucs2_to_string(devinfo + off, devinfo_len - off);
            }

            free(devinfo);
            fprintf(stderr, "[libzune] GetDeviceInfo: model='%s' serial='%s'\n",
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
            memcpy(&dev->device_id_raw, data, 4);
            dev->device_family = (dev->device_id_raw >> 24) & 0xFF;
            const char *family_name = "unknown";
            switch (dev->device_family) {
            case 0x00: family_name = "Keel (Zune 30)"; break;
            case 0x02: family_name = "Scorpius (Flash 4/8/16)"; break;
            case 0x03: family_name = "Draco (HDD 80/120)"; break;
            case 0x06: family_name = "Pavo (Zune HD)"; break;
            }
            fprintf(stderr, "[libzune] device family: %s (0xD21A = 0x%08X)\n",
                    family_name, dev->device_id_raw);
        } else {
            fprintf(stderr, "[libzune] device property 0xD21A not available, family unknown\n");
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

    fprintf(stderr, "[libzune] connected: %s (%s), battery: %u%%, storage: %llu/%llu\n",
            dev->name, dev->model, dev->battery,
            (unsigned long long)dev->storage_free,
            (unsigned long long)dev->storage_total);

    return dev;
}

/* ---- Disconnect ---- */

void zune_sever(ZuneDevice *dev) {
    if (!dev) return;

    dev->disconnecting = 1;
    dev->cached_library = NULL;  /* borrowed ref — don't free, caller owns it */
    usleep(300000);

    ptp_close_session(&dev->ptp);

    if (dev->usb.backend)
        dev->usb.backend->close(&dev->usb);

    free(dev->name);
    free(dev->model);
    free(dev->serial);
    free(dev->storage_desc);
    free(dev);
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
    if (!dev || dev->storage_id == 0) return -1;
    mtp_storage_info_t sinfo;
    memset(&sinfo, 0, sizeof(sinfo));
    if (mtp_get_storage_info(&dev->ptp, dev->storage_id, &sinfo) != 0) {
        fprintf(stderr, "[libzune] refresh_storage: GetStorageInfo failed (%s 0x%04X)\n",
                zune_autopsy_name(dev->ptp.last_response), dev->ptp.last_response);
        return -1;
    }
    dev->storage_total = sinfo.max_capacity;
    dev->storage_free  = sinfo.free_space;
    mtp_free_storage_info(&sinfo);
    return 0;
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

    fprintf(stderr, "[libzune] model detection failed — family=0x%02X model='%s'\n",
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
    if (dev->disconnecting) return 0;
    if (!dev->usb.backend) return 0;
    return 1;
}

/* ---- Folder operations ---- */

uint32_t zune_forge_folder(ZuneDevice *dev, const char *name, uint32_t parent_id) {
    if (!dev || !name) return 0;
    uint32_t new_handle = 0;
    if (mtp_create_folder(&dev->ptp, parent_id, dev->storage_id, name, &new_handle) < 0) {
        fprintf(stderr, "[libzune] create folder '%s' failed\n", name);
        return 0;
    }
    fprintf(stderr, "[libzune] created folder: %s (id=%u)\n", name, new_handle);
    return new_handle;
}

int zune_get_folders(ZuneDevice *dev, ZuneFolderEntry **out) {
    if (!dev || !out) return -1;
    *out = NULL;

    /* Get all folder handles (format=Association, parent=root) */
    uint32_t *handles = NULL;
    int nhandles = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_Association,
                                0x00000000, &handles, &nhandles) < 0 || nhandles == 0) {
        free(handles);
        return 0;
    }

    ZuneFolderEntry *entries = calloc(nhandles, sizeof(ZuneFolderEntry));
    if (!entries) { free(handles); return -1; }

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
            /* A dead transport burns a full USB timeout per call —
             * bail with what we have instead of freezing the breach. */
            fprintf(stderr, "[libzune] get_folders: transport looks dead, "
                    "stopping after %d/%d\n", i + 1, nhandles);
            break;
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
    if (dev) dev->cancel_requested = 1;
}

int zune_is_aborted(ZuneDevice *dev) {
    return dev ? dev->cancel_requested : 0;
}

void zune_clear_abort(ZuneDevice *dev) {
    if (dev) dev->cancel_requested = 0;
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
    case 0xA801: return "MTP_InvalidObjectPropCode";
    case 0xA802: return "MTP_InvalidObjectPropFormat";
    case 0xA803: return "MTP_InvalidObjectPropValue";
    case 0xA806: return "MTP_InvalidDataset";
    case 0xA808: return "MTP_ObjectTooLarge";
    case 0x02FF: return "TransportError";
    default:     return "Unknown";
    }
}

/* ---- Rename device ---- */

int zune_rename(ZuneDevice *dev, const char *new_name) {
    if (!dev || !new_name) return -1;

    /* Encode as MTP string (UCS-2LE wire format) */
    uint32_t wire_len = 0;
    uint8_t *wire = mtp_string_to_ucs2(new_name, &wire_len);
    if (!wire || wire_len == 0) {
        zune_set_error("Failed to encode device name to UCS-2");
        free(wire);
        return -1;
    }

    /* Set Friendly Name device property (0xD402) */
    int ret = ptp_set_device_prop_value(&dev->ptp, 0xD402, wire, wire_len);
    free(wire);

    if (ret == 0) {
        free(dev->name);
        dev->name = strdup(new_name);
        fprintf(stderr, "[libzune] device renamed to '%s'\n", new_name);
    } else {
        zune_set_error("Failed to set device name property (0xD402)");
    }

    return ret;
}
