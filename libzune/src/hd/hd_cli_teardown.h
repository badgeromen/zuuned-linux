/* Private research CLI only. Never send CloseSession after uncertain framing.
 * Ownership mirrors src/device.c:zune_sever; cached_library is borrowed. */
#ifndef ZUNETOOL_HD_CLI_TEARDOWN_H
#define ZUNETOOL_HD_CLI_TEARDOWN_H
#include "zune_internal.h"

static inline int hd_cli_transaction_uncertain(int result, uint16_t response) {
    /* A complete standard/vendor refusal retains transaction boundaries.
     * Local errors, absent responses and failure paired with OK do not. */
    return (response & 0xf000) != 0x2000 && (response & 0xf000) != 0xa000
        ? 1 : result != 0 && response == PTP_RC_OK;
}

static inline void hd_cli_sever(ZuneDevice *dev, int uncertain) {
    if (!dev) return;
    if (!uncertain) { zune_sever(dev); return; }
    ZUNE_FLAG_SET(dev->disconnecting, 1);
    dev->cached_library = NULL;
    if (dev->usb.backend && dev->usb.backend->close)
        dev->usb.backend->close(&dev->usb);
    free(dev->name);
    free(dev->model);
    free(dev->serial);
    free(dev->storage_desc);
    free(dev);
}
#endif
