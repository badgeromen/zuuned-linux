/*
 * libzune — finalize.c
 * Sync finalization: signal device to rebuild its media database.
 */

#include "zune_internal.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int zune_finalize(ZuneDevice *dev) {
    if (!dev) return -1;

    int ret;

    /* 1. ReportAddedDeletedItems (0x9201) — WMPPD operation.
     * Prime candidate for triggering DB rebuild. */
    fprintf(stderr, "[libzune-finalize] trying ReportAddedDeletedItems (0x9201)...\n");
    ret = mtp_vendor_operation(&dev->ptp, 0x9201, NULL, 0, NULL, NULL);
    fprintf(stderr, "[libzune-finalize] 0x9201 result: %d (0=ok)\n", ret);

    /* 2. CleanDataStore (0x9108) — WMDRMPD operation. */
    fprintf(stderr, "[libzune-finalize] trying CleanDataStore (0x9108)...\n");
    ret = mtp_vendor_operation(&dev->ptp, 0x9108, NULL, 0, NULL, NULL);
    fprintf(stderr, "[libzune-finalize] 0x9108 result: %d (0=ok)\n", ret);

    /* 3. ReportAcquiredItems (0x9202) — another WMPPD operation. */
    fprintf(stderr, "[libzune-finalize] trying ReportAcquiredItems (0x9202)...\n");
    ret = mtp_vendor_operation(&dev->ptp, 0x9202, NULL, 0, NULL, NULL);
    fprintf(stderr, "[libzune-finalize] 0x9202 result: %d (0=ok)\n", ret);

    /*
     * DISABLED: Probing device properties and unknown vendor operations.
     * These STALL the USB pipe on the Zune, breaking the connection for
     * all subsequent operations. The Zune disconnects/reconnects after
     * hitting unsupported vendor opcodes (0x9204+).
     *
     * Only CleanDataStore (0x9108) above is safe. The rest are experimental
     * probes that should NOT run during normal sync.
     *
     * TODO: Re-enable individual operations once we know which ones
     * the Zune actually supports without stalling.
     */

#if 0
    /* 4. Probe Zune device properties (read-only). */
    fprintf(stderr, "[libzune-finalize] probing Zune device properties...\n");
    uint16_t zune_props[] = { 0xD181, 0xD132, 0xD215, 0xD216 };
    for (int i = 0; i < 4; i++) {
        uint8_t *data = NULL;
        uint32_t len = 0;
        ret = ptp_get_device_prop_value(&dev->ptp, zune_props[i], &data, &len);
        fprintf(stderr, "[libzune-finalize] prop 0x%04X: %s\n",
                zune_props[i], ret == 0 ? "readable" : "failed");
        free(data);
    }

    /* 5. Probe unknown vendor operations from mtp-detect. */
    fprintf(stderr, "[libzune-finalize] probing unknown vendor operations...\n");
    uint16_t unknown_ops[] = {
        0x9204, 0x9217, 0x9218, 0x9219, 0x921A, 0x921B, 0x921C, 0x921D,
        0x9220, 0x9221, 0x9222, 0x9223, 0x9224, 0x9225, 0x9226, 0x9227,
        0x9228, 0x9229, 0x922A, 0x922B, 0x922C, 0x922D, 0x922E, 0x922F,
        0x9230, 0x9231, 0x9232, 0x9240, 0x9242, 0x9243
    };
    int num_ops = (int)(sizeof(unknown_ops) / sizeof(unknown_ops[0]));
    for (int i = 0; i < num_ops; i++) {
        ret = mtp_vendor_operation(&dev->ptp, unknown_ops[i], NULL, 0, NULL, NULL);
        fprintf(stderr, "[libzune-finalize] op 0x%04X: %s\n",
                unknown_ops[i], ret == 0 ? "OK" : "failed");
    }

    /* 6. Close Media Session (0x9171). */
    fprintf(stderr, "[libzune-finalize] trying CloseMediaSession (0x9171)...\n");
    ret = mtp_vendor_operation(&dev->ptp, 0x9171, NULL, 0, NULL, NULL);
    fprintf(stderr, "[libzune-finalize] 0x9171 result: %d (0=ok)\n", ret);
#endif

    fprintf(stderr, "[libzune-finalize] done — check if Zune re-indexed\n");
    return 0;
}

/*
 * zune_rename_item — Rename an item on the device.
 *
 * Sets the MTP Name property (0xDC44) — the string every Zune UI list
 * displays. Works on tracks, videos, albums, and playlists. The
 * on-disk ObjectFileName is left untouched (renaming it can orphan
 * store paths on HDD models; the UI never shows it).
 */
int zune_rename_item(ZuneDevice *dev, uint32_t item_id, const char *new_name) {
    if (!dev || item_id == 0 || !new_name || !new_name[0]) return -1;
    int ret = mtp_set_object_prop_value_str(&dev->ptp, item_id,
                                            MTP_OPC_Name, new_name);
    fprintf(stderr, "[libzune] rename %u -> '%s': %s\n",
            item_id, new_name, ret == 0 ? "OK" : "FAILED");
    return ret;
}

/*
 * zune_sync_notify — 0x922A ZUNE_SyncNotify (wire-confirmed).
 *
 * Pre-operation sync notification: tells the device the item name and
 * batch progress before a file operation. The official client sends
 * one before every transfer; it drives the device's on-screen sync
 * status and (on HDD models) may serve as a seek hint.
 *
 * Payload (512 bytes; field semantics corrected against
 * capture-bulkreading.pcapng — a real 1,296-photo host->device sync):
 *   [0:4]   op_kind                  uint32 LE  0=write to device,
 *                                               1=read from device
 *   [4:8]   progress_before (0-100)  uint32 LE  PERCENT, not units
 *   [8:12]  progress_after  (0-100)  uint32 LE
 *   [12:16] item_index (1-based)     uint32 LE
 *   [16:20] total_items              uint32 LE
 *   [20:]   raw UCS-2LE name, null-terminated, zero-padded
 *
 * The first cut hardcoded op_kind=1 (the value from device->host copy
 * captures) — Pavo ACKed every call but never showed sync progress;
 * the display presumably keys off write-direction notifies.
 *
 * Best-effort: failure is logged and ignored (older firmware may not
 * accept it; transfers work without it).
 */
int zune_sync_notify(ZuneDevice *dev, const char *name, uint32_t op_kind,
                     uint32_t item_index, uint32_t total_items,
                     uint32_t progress_before, uint32_t progress_after) {
    if (!dev) return -1;

    /* 530 bytes = the wire-observed 542-byte container minus its
     * 12-byte header (512-byte body + 18-byte zero tail). The first
     * attempt sent 512 and Pavo refused every call with 0x2002 —
     * Zune firmware cares about exact framing. */
    uint8_t payload[530];
    memset(payload, 0, sizeof(payload));
    payload[0] = (uint8_t)(op_kind);
    payload[1] = (uint8_t)(op_kind >> 8);
    payload[2] = (uint8_t)(op_kind >> 16);
    payload[3] = (uint8_t)(op_kind >> 24);
    payload[4]  = (uint8_t)(progress_before);
    payload[5]  = (uint8_t)(progress_before >> 8);
    payload[6]  = (uint8_t)(progress_before >> 16);
    payload[7]  = (uint8_t)(progress_before >> 24);
    payload[8]  = (uint8_t)(progress_after);
    payload[9]  = (uint8_t)(progress_after >> 8);
    payload[10] = (uint8_t)(progress_after >> 16);
    payload[11] = (uint8_t)(progress_after >> 24);
    payload[12] = (uint8_t)(item_index);
    payload[13] = (uint8_t)(item_index >> 8);
    payload[14] = (uint8_t)(item_index >> 16);
    payload[15] = (uint8_t)(item_index >> 24);
    payload[16] = (uint8_t)(total_items);
    payload[17] = (uint8_t)(total_items >> 8);
    payload[18] = (uint8_t)(total_items >> 16);
    payload[19] = (uint8_t)(total_items >> 24);

    /* Raw UCS-2LE name (mtp_string_to_ucs2 is length-prefixed MTP
     * format — skip its 1-byte count to get the bare string). */
    if (name && name[0]) {
        uint32_t wire_len = 0;
        uint8_t *wire = mtp_string_to_ucs2(name, &wire_len);
        if (wire && wire_len > 1) {
            uint32_t copy = wire_len - 1;               /* skip count byte */
            /* String lives in the 512-byte body only (ends at 512,
             * null-terminated); the 18-byte tail stays all-zero. */
            if (copy > 512 - 20 - 2)
                copy = 512 - 20 - 2;
            copy &= ~1u;                                /* whole UCS-2 units */
            memcpy(payload + 20, wire + 1, copy);
        }
        free(wire);
    }

    uint16_t response = 0;
    int ret = ptp_transaction(&dev->ptp, 0x922A, NULL, 0, PTP_DP_SENDDATA,
                              payload, sizeof(payload), NULL, NULL,
                              &response, NULL, NULL);
    if (ret != 0 || response != PTP_RC_OK)
        fprintf(stderr, "[libzune] sync_notify (0x922A): resp 0x%04X (ignored)\n",
                response);
    return (ret == 0 && response == PTP_RC_OK) ? 0 : -1;
}
