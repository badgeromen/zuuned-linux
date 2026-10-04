# Why C?

## The Case for Pure C in a Zune Device Library

### The Problem with Existing Solutions

The only other open-source Zune sync implementation with full MTP/MTPZ support is [zune-explorer](https://github.com/NiceBeard/zune-explorer), built in JavaScript/Electron. It's excellent work — they reverse-engineered the MTPZ handshake, the ZMDB binary format, and built a pure JS MTP stack. But it comes with trade-offs:

- **~200MB Electron runtime** just to talk to a USB device
- **Node.js `usb` module** wraps libusb in JavaScript bindings — adding latency to every USB operation
- **No library reuse** — you can't call their MTP stack from Swift, Python, Rust, or any other language without embedding a Node.js runtime
- **Garbage collector pauses** during USB transfers can cause timeouts on timing-sensitive MTP operations
- **Single-threaded event loop** means USB I/O blocks the UI

### Why C Solves This

libzune is pure C99 with no runtime dependencies beyond the system C library, libusb, and libgcrypt:

**1. Universal FFI (Foreign Function Interface)**
Any language can call C functions. libzune works from:
- **Swift** (macOS/iOS via bridging header)
- **Python** (via ctypes or cffi)
- **Rust** (via extern "C" FFI)
- **Go** (via cgo)
- **C++** (directly)
- **Java/Kotlin** (via JNI)
- **C#/.NET** (via P/Invoke)

One library, every platform, every language.

**2. Zero Overhead USB Communication**
C talks directly to libusb. No bindings, no marshalling, no garbage collector. When you call `libusb_bulk_transfer()`, you're one function call away from the kernel USB driver. This matters for:
- MTPZ handshake timing (RSA + AES operations must complete within USB timeouts)
- Large file transfers (streaming from fd to USB without copying)
- ZMDB reads (raw bulk pipe operations that bypass PTP)

**3. Minimal Binary Size**
libzune.dylib is ~80KB. The entire library — device management, MTP operations, ZMDB parsing, transcoding, thumbnails, playlists, album hierarchy — in 80KB. Compare to the ~200MB Electron app.

**4. No Runtime Dependencies**
libzune needs:
- libmtp (vendored, compiled from source with Zune patches)
- libusb (system library, available on every OS)
- libgcrypt (for MTPZ authentication)
- ffmpeg CLI (runtime only, for transcoding — not linked)

That's it. No Node.js, no npm, no Electron, no V8 engine.

**5. Predictable Performance**
C has no garbage collector, no JIT compilation, no event loop scheduling. When you call `zune_send_track()`, it executes deterministically. This prevents the USB timeout issues that plague higher-level language implementations on macOS.

**6. The Zune Itself is C**
The Zune firmware, the Windows Zune Software, and libmtp are all C/C++. The MTP protocol is defined in terms of byte buffers, struct layouts, and USB bulk transfers — concepts that map 1:1 to C. Writing the library in C means we work at the same abstraction level as the protocol itself.

### What We Keep from zune-explorer

Their JavaScript implementation is the best reference for:
- ZMDB binary format (reverse-engineered from XuneSyncLibrary)
- MTPZ authentication protocol (RSA/AES/CMAC handshake)
- Artist object creation (format 0xB218)
- Album hierarchy with ArtistId linking (0xDAB9)

We port the ALGORITHMS, not the runtime. The ZMDB parser logic is the same — but instead of `Buffer.readUint32LE()`, we use `*(uint32_t*)(buf + offset)`. Same data, zero overhead.

### Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| macOS (Apple Silicon) | ✅ Full | Homebrew libgcrypt/libusb, sips for image processing |
| macOS (Intel) | ✅ Full | Same as above |
| Linux (x86_64) | ✅ Full | System packages, original development platform |
| Linux (ARM) | ✅ Full | Raspberry Pi, etc. |
| Windows | 🔄 Planned | MinGW or MSVC, WinUSB driver |
| FreeBSD/OpenBSD | 🔄 Planned | Should work with minimal changes |

### The Bottom Line

C is the right language for a USB device library because:
- It speaks the same language as the hardware
- It can be called from any other language
- It has zero runtime overhead
- It produces tiny binaries
- It runs everywhere

libzune isn't a C library because we couldn't use something else. It's a C library because nothing else makes sense for this job.
