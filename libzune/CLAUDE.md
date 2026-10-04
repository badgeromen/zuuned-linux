# CLAUDE.md — AI Assistant Guide for libzune

## Project Overview

libzune is a C library for full Zune device management. Native PTP/MTP/MTPZ stack — no vendored libmtp dependency. Handles MTPZ authentication, music/video/photo sync, TV series metadata (vendor properties), UCS-2 descriptions, album art, transcode profiles, and photo album management.

Pure C. No GTK. No GLib. Platform-specific USB backend selected at compile time.

## Build

```bash
make                    # Build (auto-detects changed files)
make clean && make      # Full rebuild
```

Produces `libzune.dylib` (macOS) or `libzune.so` (Linux) + `libzune.a` (static).

### Dependencies

- **libgcrypt** — MTPZ authentication (RSA/AES)
- **macOS:** IOKit framework (DEXT IOUserClient backend)
- **Linux:** libusb-1.0
- **Runtime:** ffmpeg CLI (for transcode/thumbnail — being replaced with C API)

## Architecture

```
┌─────────────────────────────────────────────┐
│ Public API (include/zune.h)                 │
│ "Zune Rebellion" themed function names      │
│ breach, smuggle, forge, brand, purge, etc.  │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────┴──────────────────────────┐
│ Zune Operations Layer                       │
│ device.c, track.c, video.c, photo.c,       │
│ album.c, playlist.c, thumbnail.c, zmdb.c   │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────┴──────────────────────────┐
│ MTP Operations (mtp.c)                      │
│ SendObjectPropList, GetObject, SetPropValue  │
│ References, Folders, Representative Samples  │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────┴──────────────────────────┐
│ PTP Protocol (ptp.c)                        │
│ Container framing, sessions, transactions   │
│ Split header/data mode (Zune/WMP compat)    │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────┴──────────────────────────┐
│ MTPZ Authentication (mtpz.c)                │
│ RSA/AES handshake, embedded keys            │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────┴──────────────────────────┐
│ USB Backend (usb.h abstraction)             │
│ macOS: driverkit_usb.c (IOUserClient→DEXT)  │
│ Linux: usb_libusb.c (libusb-1.0)           │
└─────────────────────────────────────────────┘
```

## Code Conventions

- C99, no external framework dependencies (no GLib, no GTK)
- All public functions prefixed `zune_` with rebellion-themed verbs
- Internal MTP functions prefixed `mtp_`
- PTP protocol functions prefixed `ptp_`
- Error handling: `fprintf(stderr, "[prefix] ...")`, return -1 or 0/NULL
- Thread-local error string via `zune_set_error()` / `zune_get_error()`

## Zune Rebellion API — Function Naming Convention

| Verb | Meaning | Example |
|------|---------|---------|
| `breach` | Connect to device | `zune_breach()` |
| `sever` | Disconnect | `zune_sever()` |
| `smuggle` | Send file to device | `zune_smuggle_track()` |
| `forge` | Create MTP object | `zune_forge_album()` |
| `brand` | Set album art | `zune_brand()` |
| `rewire` | Update existing object | `zune_rewire_album()` |
| `purge` | Delete from device | `zune_purge_track()` |
| `extract` | Download from device | `zune_extract_track()` |
| `infiltrate` | Scan ZMDB database | `zune_infiltrate()` |
| `arm` | Transcode for Zune | `zune_arm_audio()` |
| `unjam` | Clear USB stall | `zune_unjam()` |
| `abort` | Cancel transfer | `zune_abort()` |
| `probe` | Extract metadata | `zune_probe()` |
| `get_*` | Read info (standard) | `zune_get_battery()` |
| `free_*` | Free memory (standard) | `zune_free_tracks()` |

## Important Patterns

### Split Header/Data Mode
The Zune requires PTP data containers to be sent as TWO separate USB transfers: 12-byte header first, then payload. Auto-detected during GetDeviceInfo. Without this, all MTP operations return 0x2002.

### SendObjectPropList (0x9808)
The Zune requires album/artist objects to be created atomically with all metadata via SendObjectPropList. Post-creation SetObjectPropValue for AlbumArtist returns 0xA801.

### MTPZ Authentication
Required before any write operations. Uses RSA/AES handshake via libgcrypt. Keys embedded in `mtpz_keys.h` with `~/.mtpz-data` override.

### Embedded Keys
MTPZ keys are compiled into the binary (`mtpz_keys.h`). Users can override by placing custom keys in `~/.mtpz-data`.

## USB Backends

### macOS (driverkit_usb.c)
Talks to ZuneUSBDriver DEXT via IOUserClient. Uses `IOServiceNameMatching("ZuneUSBDriver")` to find the driver, `IOConnectCallMethod` for bulk transfers.

### Linux (usb_libusb.c)
Standard libusb-1.0. Claims interface, bulk transfers, clear halt.

### Adding a new backend
Implement `zune_usb_backend_t` function table (open, close, bulk_read, bulk_write, clear_halt, reset). Add to Makefile with conditional compilation.

## Key Files

| File | What it does |
|------|-------------|
| `include/zune.h` | Public API — all Rebellion-named functions |
| `src/zune_internal.h` | Internal header — ZuneDevice struct, shared helpers |
| `src/device.c` | Connection lifecycle, device info, folders |
| `src/track.c` | Music operations (smuggle, purge, extract) |
| `src/video.c` | Video operations (movies, TV, music videos) |
| `src/photo.c` | Photo operations |
| `src/album.c` | Album/artist forge + rewire + link |
| `src/playlist.c` | Playlist forge + rewire |
| `src/thumbnail.c` | Album art (brand) + thumbnails (grab) |
| `src/zmdb.c` | ZMDB binary database parser (infiltrate) |
| `src/transcode.c` | Audio/video transcoding (arm) |
| `src/util.c` | Filename parsing, thumbnail generation |
| `src/usb_recovery.c` | USB stall recovery (unjam) |
| `src/finalize.c` | Post-sync device finalization |
| `src/ptp.c` | PTP protocol layer |
| `src/mtp.c` | MTP operations layer |
| `src/mtpz.c` | MTPZ authentication |
| `src/mtpz_keys.h` | Embedded MTPZ key data |
| `src/usb.h` | USB backend abstraction |
| `src/driverkit_usb.c` | macOS DEXT backend |
| `src/usb_libusb.c` | Linux libusb backend |

## Debugging

- All logs go to stderr with `[prefix]` tags
- `[libzune]` — high-level operations
- `[ptp]` — PTP protocol (CMD/RESP hex dumps when enabled)
- `[mtp]` — MTP operations
- `[mtpz]` — MTPZ authentication
- `[driverkit-usb]` — macOS DEXT backend
- `[libusb]` — Linux USB backend
- `[zunedb]` — ZMDB parser

## Development Methodology

### Core Principles
1. **Full-scale solutions, not shortcuts** — Address root causes with proper architecture. If USB device ownership is the problem, build a driver. Don't patch symptoms.
2. **Think before coding** — Analyze root cause. Read code. Research the problem. Design the solution. Only then implement.
3. **Document everything** — Every finding, decision, and architecture choice gets documented.

### Agent Workflow (for non-trivial tasks)
1. **Architect (Claude + User)** — Define problem, design solution, set goals
2. **PM Agent** — Tracks goals from design phase, validates all goals met at completion
3. **Developer Agents** — Execute implementation. Receive full context: file paths, docs, architecture. No TODOs or placeholders.
4. **QA Agents** — Validate changes post-implementation.

### Documentation Standards
- **Exhaustive TOC** in `docs/TOC.md` — every function with file:line references
- **TOC stays current** — when code changes, TOC gets updated
- **No external dependencies** — everything bundled, app is self-contained

### Skills & Domain Knowledge
- C systems programming — kernel-level, driver development, USB subsystems
- macOS DriverKit — USB driver extensions, IOKit, entitlements, code signing
- MTP/PTP protocols — device communication, vendor extensions, MTPZ auth
- FFmpeg — transcoding pipelines, codec internals, container formats (migrating to C API)

## Documentation Map

See `docs/TOC.md` for the master function index.
