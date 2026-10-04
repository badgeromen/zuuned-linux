/*
 * driverkit_usb.c — macOS DriverKit USB backend for libzune
 *
 * Implements the zune_usb_backend_t interface using IOKit's
 * IOUserClient API to communicate with the ZuneUSBDriver DEXT.
 *
 * The DEXT handles raw USB communication (bulk read/write, clear halt,
 * reset) and exposes it through external methods via IOConnectCallMethod.
 * This file is the client side of that interface.
 *
 * Only compiled on macOS.
 *
 * Part of libzune.
 */

#ifdef __APPLE__

#include "usb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>

/* ---- DEXT external method selectors (must match ZuneUSBDriverShared.h) ---- */

enum {
    kZuneUSBOpen              = 0,
    kZuneUSBClose             = 1,
    kZuneUSBBulkRead          = 2,
    kZuneUSBBulkWrite         = 3,
    kZuneUSBClearHalt         = 4,
    kZuneUSBGetEndpointInfo   = 5,
    kZuneUSBSetConfiguration  = 6,
    kZuneUSBResetDevice       = 7,
};

/* Endpoint info structure returned by kZuneUSBGetEndpointInfo */
typedef struct {
    uint8_t  config;
    uint8_t  interface_num;
    uint8_t  altsetting;
    uint8_t  bulk_in_endpoint;
    uint16_t bulk_in_maxpacket;
    uint8_t  bulk_out_endpoint;
    uint16_t bulk_out_maxpacket;
    uint8_t  interrupt_endpoint;
    uint8_t  _padding[3];
} ZuneUSBEndpointInfo;

/* ---- Backend private data ---- */

typedef struct {
    io_service_t  service;      /* IOKit service reference */
    io_connect_t  connection;   /* IOUserClient connection */
} driverkit_backend_data_t;

/* ---- Helper: get backend data from handle ---- */

static inline driverkit_backend_data_t *get_data(zune_usb_handle_t *handle)
{
    return (driverkit_backend_data_t *)handle->backend_data;
}

/* ---- Backend function implementations ---- */

static int driverkit_open(zune_usb_handle_t *handle, uint16_t vid, uint16_t pid)
{
    kern_return_t kr;
    io_service_t service = IO_OBJECT_NULL;
    io_connect_t connection = IO_OBJECT_NULL;

    (void)vid;
    (void)pid;

    /*
     * Build a matching dictionary for our DEXT class name.
     * IOServiceMatching returns a retained dictionary that
     * IOServiceGetMatchingService consumes.
     */
    CFMutableDictionaryRef matching = IOServiceNameMatching("ZuneUSBDriver");
    if (!matching) {
        fprintf(stderr, "[driverkit-usb] failed to create matching dictionary\n");
        return ZUNE_USB_ERROR;
    }

    /*
     * Find the driver service. This returns the first matching service.
     * The Zune will only ever have one instance.
     */
    service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
    /* matching is consumed by the call above, do not release it */

    if (service == IO_OBJECT_NULL) {
        fprintf(stderr, "[driverkit-usb] ZuneUSBDriver service not found "
                "(vid=0x%04X pid=0x%04X)\n", vid, pid);
        return ZUNE_USB_ERROR;
    }

    /*
     * Open a connection to the DEXT's IOUserClient.
     * Type 0 is the default client type.
     */
    kr = IOServiceOpen(service, mach_task_self(), 0, &connection);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "[driverkit-usb] IOServiceOpen failed: 0x%08X\n", kr);
        IOObjectRelease(service);
        return ZUNE_USB_ERROR;
    }

    /*
     * Call kZuneUSBOpen to tell the DEXT we are claiming the device.
     * No inputs or outputs needed.
     */
    kr = IOConnectCallMethod(connection, kZuneUSBOpen,
                             NULL, 0,    /* no scalar input */
                             NULL, 0,    /* no struct input */
                             NULL, NULL, /* no scalar output */
                             NULL, NULL  /* no struct output */);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "[driverkit-usb] kZuneUSBOpen failed: 0x%08X\n", kr);
        IOServiceClose(connection);
        IOObjectRelease(service);
        return ZUNE_USB_ERROR;
    }

    /*
     * Query endpoint info from the DEXT. The driver discovers
     * endpoints during Start() and caches them for us.
     */
    ZuneUSBEndpointInfo epinfo;
    memset(&epinfo, 0, sizeof(epinfo));
    size_t epinfo_size = sizeof(epinfo);

    kr = IOConnectCallMethod(connection, kZuneUSBGetEndpointInfo,
                             NULL, 0,           /* no scalar input */
                             NULL, 0,           /* no struct input */
                             NULL, NULL,        /* no scalar output */
                             &epinfo, &epinfo_size  /* struct output */);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "[driverkit-usb] kZuneUSBGetEndpointInfo failed: 0x%08X\n", kr);
        IOConnectCallMethod(connection, kZuneUSBClose,
                            NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
        IOServiceClose(connection);
        IOObjectRelease(service);
        return ZUNE_USB_ERROR;
    }

    if (epinfo_size < sizeof(ZuneUSBEndpointInfo)) {
        fprintf(stderr, "[driverkit-usb] endpoint info too small: %zu < %zu\n",
                epinfo_size, sizeof(ZuneUSBEndpointInfo));
        IOConnectCallMethod(connection, kZuneUSBClose,
                            NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
        IOServiceClose(connection);
        IOObjectRelease(service);
        return ZUNE_USB_ERROR;
    }

    /* Populate handle endpoint fields */
    handle->bulk_in_ep       = epinfo.bulk_in_endpoint;
    handle->bulk_in_maxpacket  = epinfo.bulk_in_maxpacket;
    handle->bulk_out_ep      = epinfo.bulk_out_endpoint;
    handle->bulk_out_maxpacket = epinfo.bulk_out_maxpacket;
    handle->interrupt_ep     = epinfo.interrupt_endpoint;

    fprintf(stderr, "[driverkit-usb] opened: bulk_in=0x%02X (%u) "
            "bulk_out=0x%02X (%u) intr=0x%02X\n",
            handle->bulk_in_ep, handle->bulk_in_maxpacket,
            handle->bulk_out_ep, handle->bulk_out_maxpacket,
            handle->interrupt_ep);

    /* Allocate and store backend-private data */
    driverkit_backend_data_t *data = calloc(1, sizeof(driverkit_backend_data_t));
    if (!data) {
        fprintf(stderr, "[driverkit-usb] failed to allocate backend data\n");
        IOConnectCallMethod(connection, kZuneUSBClose,
                            NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
        IOServiceClose(connection);
        IOObjectRelease(service);
        return ZUNE_USB_ERROR;
    }

    data->service    = service;
    data->connection = connection;
    handle->backend_data = data;

    return ZUNE_USB_OK;
}

static int driverkit_close(zune_usb_handle_t *handle)
{
    if (!handle || !handle->backend_data)
        return ZUNE_USB_ERROR;

    driverkit_backend_data_t *data = get_data(handle);
    kern_return_t kr;

    /* Tell the DEXT we are releasing the device */
    kr = IOConnectCallMethod(data->connection, kZuneUSBClose,
                             NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
    if (kr != KERN_SUCCESS)
        fprintf(stderr, "[driverkit-usb] kZuneUSBClose failed: 0x%08X\n", kr);

    /* Close the IOUserClient connection */
    IOServiceClose(data->connection);

    /* Release the service reference */
    IOObjectRelease(data->service);

    free(data);
    handle->backend_data = NULL;

    fprintf(stderr, "[driverkit-usb] closed\n");
    return ZUNE_USB_OK;
}

static int driverkit_bulk_read(zune_usb_handle_t *handle, uint8_t endpoint,
                               uint8_t *data, uint32_t length,
                               uint32_t *actual_length, uint32_t timeout_ms)
{
    if (!handle || !handle->backend_data) {
        fprintf(stderr, "[driverkit-usb] bulk_read: invalid handle\n");
        return ZUNE_USB_ERROR;
    }

    driverkit_backend_data_t *bd = get_data(handle);

    /*
     * Scalar inputs: endpoint address and timeout.
     * Struct output: the read buffer. The DEXT fills it and
     * returns the actual byte count via outputStructCnt.
     */
    uint64_t scalar_input[2] = {
        (uint64_t)endpoint,
        (uint64_t)timeout_ms
    };

    size_t output_size = (size_t)length;
    uint64_t scalar_output[1] = {0};
    uint32_t scalar_output_count = 1;

    kern_return_t kr = IOConnectCallMethod(
        bd->connection,
        kZuneUSBBulkRead,
        scalar_input, 2,                       /* scalar input: endpoint, timeout */
        NULL, 0,                               /* no struct input */
        scalar_output, &scalar_output_count,   /* scalar output: actual bytes */
        data, &output_size                     /* struct output: read buffer */
    );

    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "[driverkit-usb] bulk_read failed: ep=0x%02X len=%u "
                "err=0x%08X\n", endpoint, length, kr);
        if (actual_length)
            *actual_length = 0;
        return ZUNE_USB_ERROR;
    }

    if (actual_length)
        *actual_length = (uint32_t)scalar_output[0];

    return ZUNE_USB_OK;
}

static int driverkit_bulk_write(zune_usb_handle_t *handle, uint8_t endpoint,
                                const uint8_t *data, uint32_t length,
                                uint32_t *actual_length, uint32_t timeout_ms)
{
    if (!handle || !handle->backend_data) {
        fprintf(stderr, "[driverkit-usb] bulk_write: invalid handle\n");
        return ZUNE_USB_ERROR;
    }

    driverkit_backend_data_t *bd = get_data(handle);

    /*
     * Scalar inputs: endpoint address and timeout.
     * Struct input: the data buffer to write.
     * Scalar output: actual bytes written (1 element).
     */
    uint64_t scalar_input[2] = {
        (uint64_t)endpoint,
        (uint64_t)timeout_ms
    };

    uint64_t scalar_output[1] = { 0 };
    uint32_t output_count = 1;

    kern_return_t kr = IOConnectCallMethod(
        bd->connection,
        kZuneUSBBulkWrite,
        scalar_input, 2,        /* scalar input: endpoint, timeout */
        data, (size_t)length,   /* struct input: write buffer */
        scalar_output, &output_count, /* scalar output: bytes written */
        NULL, NULL              /* no struct output */
    );

    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "[driverkit-usb] bulk_write failed: ep=0x%02X len=%u "
                "err=0x%08X\n", endpoint, length, kr);
        if (actual_length)
            *actual_length = 0;
        return ZUNE_USB_ERROR;
    }

    /*
     * If the DEXT returned actual bytes in scalar output, use that.
     * Otherwise assume the full buffer was sent (the call succeeded).
     */
    if (actual_length) {
        if (output_count > 0 && scalar_output[0] > 0)
            *actual_length = (uint32_t)scalar_output[0];
        else
            *actual_length = length;
    }

    return ZUNE_USB_OK;
}

static int driverkit_clear_halt(zune_usb_handle_t *handle, uint8_t endpoint)
{
    if (!handle || !handle->backend_data) {
        fprintf(stderr, "[driverkit-usb] clear_halt: invalid handle\n");
        return ZUNE_USB_ERROR;
    }

    driverkit_backend_data_t *bd = get_data(handle);

    uint64_t scalar_input[1] = { (uint64_t)endpoint };

    kern_return_t kr = IOConnectCallMethod(
        bd->connection,
        kZuneUSBClearHalt,
        scalar_input, 1,    /* scalar input: endpoint */
        NULL, 0,             /* no struct input */
        NULL, NULL,          /* no scalar output */
        NULL, NULL           /* no struct output */
    );

    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "[driverkit-usb] clear_halt failed: ep=0x%02X "
                "err=0x%08X\n", endpoint, kr);
        return ZUNE_USB_ERROR;
    }

    fprintf(stderr, "[driverkit-usb] clear_halt: ep=0x%02X OK\n", endpoint);
    return ZUNE_USB_OK;
}

static int driverkit_reset(zune_usb_handle_t *handle)
{
    if (!handle || !handle->backend_data) {
        fprintf(stderr, "[driverkit-usb] reset: invalid handle\n");
        return ZUNE_USB_ERROR;
    }

    driverkit_backend_data_t *bd = get_data(handle);

    kern_return_t kr = IOConnectCallMethod(
        bd->connection,
        kZuneUSBResetDevice,
        NULL, 0,    /* no scalar input */
        NULL, 0,    /* no struct input */
        NULL, NULL, /* no scalar output */
        NULL, NULL  /* no struct output */
    );

    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "[driverkit-usb] reset failed: err=0x%08X\n", kr);
        return ZUNE_USB_ERROR;
    }

    fprintf(stderr, "[driverkit-usb] device reset OK\n");
    return ZUNE_USB_OK;
}

/* ---- Static backend instance ---- */

static const zune_usb_backend_t driverkit_backend = {
    .open       = driverkit_open,
    .close      = driverkit_close,
    .bulk_read  = driverkit_bulk_read,
    .bulk_write = driverkit_bulk_write,
    .clear_halt = driverkit_clear_halt,
    .reset      = driverkit_reset,
};

const zune_usb_backend_t *zune_usb_get_backend(void)
{
    return &driverkit_backend;
}

/* ---- Device discovery ---- */

int zune_usb_find_device(zune_usb_handle_t *handle)
{
    if (!handle) {
        fprintf(stderr, "[driverkit-usb] find_device: NULL handle\n");
        return ZUNE_USB_ERROR;
    }

    memset(handle, 0, sizeof(*handle));
    handle->backend = zune_usb_get_backend();

    /*
     * Try the standard Zune PID first (covers Zune 4/8/16/30/80/120),
     * then fall back to the Zune HD PID.
     */
    fprintf(stderr, "[driverkit-usb] searching for Zune (PID 0x%04X)...\n",
            ZUNE_USB_PID_ZUNE);

    if (handle->backend->open(handle, ZUNE_USB_VID, ZUNE_USB_PID_ZUNE) == ZUNE_USB_OK) {
        fprintf(stderr, "[driverkit-usb] found Zune (PID 0x%04X)\n",
                ZUNE_USB_PID_ZUNE);
        return ZUNE_USB_OK;
    }

    fprintf(stderr, "[driverkit-usb] searching for Zune HD (PID 0x%04X)...\n",
            ZUNE_USB_PID_ZUNE_HD);

    if (handle->backend->open(handle, ZUNE_USB_VID, ZUNE_USB_PID_ZUNE_HD) == ZUNE_USB_OK) {
        fprintf(stderr, "[driverkit-usb] found Zune HD (PID 0x%04X)\n",
                ZUNE_USB_PID_ZUNE_HD);
        return ZUNE_USB_OK;
    }

    fprintf(stderr, "[driverkit-usb] no Zune device found\n");
    return ZUNE_USB_ERROR;
}

#endif /* __APPLE__ */
