# libzune project overview

libzune is the shared C99 device library behind Zuuned. It implements native
PTP/MTP/MTPZ communication with Microsoft Zune media players. It is independent
of the Qt/QML Linux application and the SwiftUI macOS application.

## Repositories

- [libzune](https://github.com/badgeromen/libzune): reusable C library, tools and protocol research.
- [Zuuned Linux](https://github.com/badgeromen/zuuned-linux): native Linux application and downloadable test releases.

## Current implementation

The [90-function API](API_REFERENCE.md) exposes connection/authentication,
media enumeration/transfers/deletion, abstract albums/artists/playlists,
representative-sample artwork, ZMDB scanning, metadata probing and diagnostics.
The [TOC](TOC.md) maps those declarations and internal functions to source.

The stack is library operations, MTP, PTP, MTPZ and a platform USB backend.
There is no vendored libmtp, GTK, GLib, Qt or language runtime dependency in the
library. Linux uses libusb and libgcrypt; its default build also links FFmpeg
libraries for metadata probing. Legacy preparation/thumbnail helpers invoke
external tools and have platform limits documented in the README/API reference.

Linux needs USB permissions, not a custom driver. The macOS backend talks to a
separate DriverKit extension that is not supplied in this repository. There is
no Windows backend here. C bindings make integration into other languages
possible, but language-specific bindings are not shipped or certified.

## Ownership and boundaries

The library provides synchronous operations. Applications own threading,
serialization, queues, local library storage, online artwork providers and user
interaction. They also own edition/disc-aware duplicate policy; the library's
name-based find helpers are deliberately much simpler.

ZMDB scans can omit fields on particular models, and playlist reads include an
MTP fallback. Device identification does not certify every model or operation.
Refer to dated hardware evidence for tested behavior, not broad completeness
claims. Firmware flashing and wireless sync are research topics, not public API.

Public source omits embedded MTPZ values. See [BUILD_WITH_MTPZ.md](BUILD_WITH_MTPZ.md)
for source-build setup. Official Zuuned binaries retain built-in authentication.
Original library code is GPL-3.0-or-later; [COPYING.md](../COPYING.md) preserves
separate notices and explains scope.

## Start here

1. Build and configure USB access using [README](../README.md).
2. Read the [API reference](API_REFERENCE.md), especially scan lifetime and teardown.
3. Start with read-only commands in [LINUX_TESTING.md](LINUX_TESTING.md).
4. Consult [WIRE_CAPTURE_FINDINGS.md](WIRE_CAPTURE_FINDINGS.md) for dated protocol evidence.
