#include "UsbWatcher.h"

#include <libusb.h>
#include <cstdio>

namespace {
constexpr uint16_t kZuneVid = 0x045e;

bool isZunePid(uint16_t pid) {
    // 0x0710 = Zune 4/8/16/30/80/120, 0x063e = Zune HD.
    // 0x0711–0x0714 appear in recovery/other modes (the zune_usb DKMS
    // module matches them too) — accept them so the UI can at least show
    // presence.
    return pid == 0x0710 || pid == 0x063e || (pid >= 0x0711 && pid <= 0x0714);
}
} // namespace

struct UsbWatcherThunk {
    static int LIBUSB_CALL hotplug(libusb_context *, libusb_device *device,
                                   libusb_hotplug_event event, void *user) {
        auto *self = static_cast<UsbWatcher *>(user);
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(device, &desc) != 0)
            return 0;
        if (desc.idVendor != kZuneVid || !isZunePid(desc.idProduct))
            return 0;

        if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
            fprintf(stderr, "[usbwatcher] Zune arrived (pid=0x%04x)\n", desc.idProduct);
            emit self->zuneArrived();
        } else {
            fprintf(stderr, "[usbwatcher] Zune left\n");
            emit self->zuneLeft();
        }
        return 0; // keep the callback registered
    }
};

UsbWatcher::UsbWatcher(QObject *parent) : QThread(parent) {}

UsbWatcher::~UsbWatcher() {
    stop();
}

void UsbWatcher::stop() {
    m_stop = true;
    if (!wait(3000)) {
        fprintf(stderr, "[usbwatcher] event loop did not stop, terminating\n");
        terminate();
        wait(1000);
    }
}

void UsbWatcher::run() {
    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) != 0) {
        fprintf(stderr, "[usbwatcher] libusb_init failed — no hotplug\n");
        return;
    }

    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        fprintf(stderr, "[usbwatcher] hotplug not supported on this platform\n");
        libusb_exit(ctx);
        return;
    }

    libusb_hotplug_callback_handle handle = 0;
    int rc = libusb_hotplug_register_callback(
        ctx,
        static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                                          LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
        LIBUSB_HOTPLUG_ENUMERATE,
        kZuneVid, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
        &UsbWatcherThunk::hotplug, this, &handle);
    if (rc != 0) {
        fprintf(stderr, "[usbwatcher] hotplug registration failed: %s\n",
                libusb_strerror(rc));
        libusb_exit(ctx);
        return;
    }

    fprintf(stderr, "[usbwatcher] watching for Zune (vid=0x%04x)\n", kZuneVid);

    while (!m_stop) {
        timeval tv{0, 250000}; // 250 ms tick so stop() stays responsive
        libusb_handle_events_timeout_completed(ctx, &tv, nullptr);
    }

    libusb_hotplug_deregister_callback(ctx, handle);
    libusb_exit(ctx);
}
