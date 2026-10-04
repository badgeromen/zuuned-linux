# Zuuned — The Complete Zune Device Manager

## What Is This?

Zuuned is the only open-source project that provides **complete** Zune device management — music, videos, photos, playlists, TV series metadata, and bidirectional sync — on Linux and macOS.

No one else has done this. Here's the landscape:

| Project | Music Sync | Video Sync | TV Metadata | Photos | Playlists | macOS | ZMDB |
|---------|-----------|-----------|------------|--------|-----------|-------|------|
| **Zuuned** | ✅ Full | ✅ Full | ✅ Full | ✅ Full | ✅ Full | ✅ | ✅ |
| Windows Zune Software | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ Dead | N/A |
| zune-explorer | ✅ Full | Browse only | ❌ | Basic | ❌ | ✅ | ✅ |
| kbhomes/libmtp-zune | Basic | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| Klar/ZuneSyncLinux | Basic | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |

## Components

### libzune (Pure C Library)
The core. 65+ exported API functions covering every aspect of Zune device communication:
- Device connection with MTPZ authentication
- Track/video/photo/playlist CRUD operations
- ZMDB fast scan (vendor opcode 0x1792)
- Album/artist MTP object hierarchy
- Video transcoding (4 Zune profiles)
- Photo preprocessing (resize + JPEG convert)
- USB stall recovery
- TV series vendor properties (season/episode/series name)

**80KB binary. Zero runtime overhead. Callable from any language.**

### zuuned (Linux GTK4 App)
The original. ~12,000 lines of C across 23 files. Full-featured Zune manager with:
- SQLite local music library
- TMDB integration for movie/TV metadata and poster art
- AcoustID/MusicBrainz lookup
- Custom GTK4 widgets (FixedGrid for media cards)
- Audio player with vinyl disc visualization
- Video transcoding with 4 Zune-specific profiles

### ZuunedMac (macOS SwiftUI App)
The port. Native macOS experience with Zune-inspired design:
- Glass morphism + graffiti grunge aesthetic
- Genre-based mood colors (device personality)
- IOKit USB auto-detection
- ZMDB fast scan for instant library loading
- Connection progress with stage reporting
- All sync features from the Linux version

### Vendored libmtp
Patched fork of libmtp 1.1.23 with:
- 3 raw PTP property setters (bypass broken enum mapping for vendor properties)
- Per-object property fetch fix in `flush_handles()` (Zune's bulk property response is broken)
- macOS device flag (`DEVICE_FLAG_UNLOAD_DRIVER`)

## What We Cracked

### The Hard Problems

1. **MTPZ Authentication** — The Zune requires a proprietary encrypted handshake (RSA + AES + CMAC) before accepting any MTP commands. Without it, all operations return "Access Denied."

2. **Vendor Properties** — Standard libmtp maps Zune vendor property codes (0xDA9A, 0xDAB5, 0xDAB6) to 0. We patched libmtp with raw PTP setters that bypass the mapping. This enables TV series metadata (series name, season, episode) which no one else can set.

3. **MetaGenre** — The Zune uses a non-standard MTP property (0xDC95) to classify videos as Movie (0x25), TV Show (0x26), Music Video (0x23), or Other (0x21). Without setting this, all videos appear in a flat list instead of categorized sections.

4. **UCS-2 Descriptions** — Video descriptions use UCS-2 encoding via the AUINT16 array property (0xDC48), not standard UTF-8 strings.

5. **Album Object Hierarchy** — The Zune reads album/artist metadata from MTP abstract objects (format 0xBA03 for albums, 0xB218 for artists), not from individual track properties. Without creating these objects and linking them via ArtistId (0xDAB9), all tracks show "Unknown Album."

6. **ZMDB Binary Database** — The Zune's internal media database can be read via vendor opcode 0x1792. We reverse-engineered the binary format (from zune-explorer's work) and implemented a C parser supporting both Classic (ZMed v2) and HD (ZMed v5) layouts.

7. **macOS USB Stack Differences** — macOS libusb has silent bulk transfer stalls, no timeout returns, and competing system daemons. Required: daemon killing, stall recovery with clearHalt, retry logic with orphan cleanup.

8. **Photo Display** — Photos require Zune re-indexing to become viewable (the firmware reads JPEG dimensions during restart). Triggered by clean USB disconnect, not by any MTP operation.

9. **ID3v2.4 Incompatibility** — The Zune silently ignores ID3v2.4 tags. MP3s must be retagged to ID3v2.3 before sending.

### What's Still Unknown

1. **ZMDB Video MetaGenre** — The ZMDB binary format stores video records but we haven't identified where MetaGenre is encoded. Bytes +8 through +31 of video records are unanalyzed.

2. **ZMDB Object IDs** — The 0x1792 request accepts a 3-byte object ID. We only use the default `[0x03, 0x92, 0x1f]`. Other IDs may select different database sections.

3. **Vendor Operations** — 30+ undocumented vendor operations (0x9204-0x9243) are listed as supported but all fail on macOS. Their purpose is unknown.

4. **Secure Time / DRM Clock** — The Zune has a DRM clock (`DRM_CLK_NOT_SET` in mtp-detect). This may affect playback of purchased content.

## Building

### libzune
```bash
cd libzune
make        # builds libzune.dylib + libzune.a
```

### ZuunedMac
```bash
cd ZuunedMac
xcodebuild -scheme Zuuned -destination 'platform=macOS' build
```

### zuuned (Linux)
```bash
make        # builds zunelinux binary
```

## License

MIT

## Credits

- **BadgerOmens** — original zuuned Linux app, libzune C library, ZuunedMac port, TV/video/photo/playlist sync
- **kbhomes** — original MTPZ authentication reverse engineering (libmtp-zune)
- **NiceBeard** — ZMDB binary format reverse engineering, zune-explorer reference implementation
- **magicisinthehole** — XuneSyncLibrary (original ZMDB format discovery)
- **libmtp contributors** — MTP protocol library foundation
