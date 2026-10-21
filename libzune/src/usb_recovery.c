/*
 * usb_recovery.c — USB stall recovery for Zune transfers
 *
 * When USB bulk transfers stall (PIPE error, timeout), the endpoints
 * need to be cleared before retrying. Without this, a single stall
 * causes the entire sync to hang forever.
 *
 * Part of libzune.
 */

#include "zune_internal.h"

int zune_unjam(ZuneDevice *dev) {
    zune_clear_error();
    if (!dev) {
        zune_set_error("Invalid device for stall recovery");
        return ZUNE_BLOWN;
    }

    const zune_usb_backend_t *backend = dev->usb.backend;
    if (!backend || !backend->clear_halt) {
        zune_set_error("No USB backend for stall recovery");
        return ZUNE_BLOWN;
    }

    int ret_in  = backend->clear_halt(&dev->usb, dev->usb.bulk_in_ep);
    int ret_out = backend->clear_halt(&dev->usb, dev->usb.bulk_out_ep);

    if (ret_in != 0)
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "clear_halt IN ep=0x%02X failed",
                dev->usb.bulk_in_ep);
    if (ret_out != 0)
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "clear_halt OUT ep=0x%02X failed",
                dev->usb.bulk_out_ep);

    ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "USB stall recovery: in=%d out=%d",
            ret_in, ret_out);
    /* Report honestly — callers deciding whether a retry is worth it
     * need to know the pipes actually cleared. */
    return (ret_in == 0 && ret_out == 0) ? ZUNE_OK : ZUNE_BLOWN;
}
