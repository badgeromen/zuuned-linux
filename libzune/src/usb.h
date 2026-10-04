/*
 * usb.h — USB backend abstraction for libzune
 *
 * Platform-specific backends (libusb, IOKit, etc.) implement the
 * zune_usb_backend_t function table. Transport code uses only
 * zune_usb_handle_t and the functions declared here.
 *
 * Part of the native PTP/MTP stack that replaces vendored libmtp.
 */

#ifndef ZUNE_USB_H
#define ZUNE_USB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* ---- Zune USB identifiers ---- */

#define ZUNE_USB_VID            0x045E  /* Microsoft */
#define ZUNE_USB_PID_ZUNE       0x0710  /* Zune 4/8/16/30/80/120 */
#define ZUNE_USB_PID_ZUNE_HD    0x063E  /* Zune HD */

/* ---- Return codes ---- */

#define ZUNE_USB_OK              0
#define ZUNE_USB_ERROR          -1

/* ---- Forward declarations ---- */

typedef struct zune_usb_handle   zune_usb_handle_t;
typedef struct zune_usb_backend  zune_usb_backend_t;

/* ---- Backend function table ----
 *
 * Each platform implements these. The backend is selected at compile
 * time (see usb_libusb.c, usb_iokit.c, etc.).
 *
 * All functions return ZUNE_USB_OK on success, ZUNE_USB_ERROR on failure.
 */

struct zune_usb_backend {
    /*
     * open — Open a USB device given vendor/product IDs.
     *        Stores platform-specific state in handle->backend_data.
     *        Returns ZUNE_USB_OK on success.
     */
    int (*open)(zune_usb_handle_t *handle, uint16_t vid, uint16_t pid);

    /*
     * close — Release the USB device and free backend_data.
     */
    int (*close)(zune_usb_handle_t *handle);

    /*
     * bulk_read — Read data from a bulk IN endpoint.
     *   endpoint:      endpoint address (e.g. handle->bulk_in_ep)
     *   data:          buffer to read into
     *   length:        maximum bytes to read
     *   actual_length: [out] bytes actually transferred
     *   timeout_ms:    timeout in milliseconds (0 = no timeout)
     */
    int (*bulk_read)(zune_usb_handle_t *handle, uint8_t endpoint,
                     uint8_t *data, uint32_t length,
                     uint32_t *actual_length, uint32_t timeout_ms);

    /*
     * bulk_write — Write data to a bulk OUT endpoint.
     *   endpoint:      endpoint address (e.g. handle->bulk_out_ep)
     *   data:          buffer to write from
     *   length:        bytes to write
     *   actual_length: [out] bytes actually transferred
     *   timeout_ms:    timeout in milliseconds (0 = no timeout)
     */
    int (*bulk_write)(zune_usb_handle_t *handle, uint8_t endpoint,
                      const uint8_t *data, uint32_t length,
                      uint32_t *actual_length, uint32_t timeout_ms);

    /*
     * clear_halt — Clear a HALT/STALL condition on an endpoint.
     */
    int (*clear_halt)(zune_usb_handle_t *handle, uint8_t endpoint);

    /*
     * reset — Reset the USB device.
     */
    int (*reset)(zune_usb_handle_t *handle);
};

/* ---- USB handle ---- */

struct zune_usb_handle {
    const zune_usb_backend_t *backend;

    /* Endpoint addresses discovered during enumeration */
    uint8_t  bulk_in_ep;
    uint8_t  bulk_out_ep;
    uint8_t  interrupt_ep;

    /* Maximum packet sizes for bulk endpoints */
    uint16_t bulk_in_maxpacket;
    uint16_t bulk_out_maxpacket;

    /* Platform-specific state (libusb_device_handle *, IOUSBInterfaceInterface **, etc.) */
    void *backend_data;
};

/* ---- Public functions ---- */

/*
 * zune_usb_get_backend — Return the compiled-in USB backend.
 *
 * Implemented per-platform (usb_libusb.c, usb_iokit.c, etc.).
 */
const zune_usb_backend_t *zune_usb_get_backend(void);

/*
 * zune_usb_find_device — Find and open a connected Zune.
 *
 * Tries ZUNE_USB_PID_ZUNE first, then ZUNE_USB_PID_ZUNE_HD.
 * On success, handle is fully initialized with endpoint addresses,
 * max packet sizes, and an open backend connection.
 *
 * Returns ZUNE_USB_OK on success, ZUNE_USB_ERROR if no Zune found.
 */
int zune_usb_find_device(zune_usb_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* ZUNE_USB_H */
