# CLAUDE.md — AI Assistant Guide for libzune

## Active hardening workstream, 2026-10-19

The user's libzune 0.3.0 hardening spec is
[docs/HARDENING_0.3.0.md](docs/HARDENING_0.3.0.md). Work on
`hardening/0.3.0`, one commit per task ID (`T1.2: ...`), with a sanitizer
regression test for each fix. Preserve every public `zune_*` name; new public
functions take themed verbs from the spec's lexicon. Only **[HW]** tasks may
change wire behavior, and each records its gate in Appendix A. Never touch
MTPZ crypto math or key material. Appendix C records baseline, deviations
and hardware evidence. October 20: every task, Tier 3 included, is
implemented and passes on the Zune HD, the Zune 30 and the Zune 80 (Draco);
Scorpius has not been tested (Appendix A), and `docs/RELEASE_0.3.0.md` is the
release draft. The same day an eight-agent validation round found and fixed
the defects in the last Appendix C entry; read it before changing the upload
engine, album forging, the ZMDB reader or the build. Uploads go through `zune_smuggle_core` (src/upload.c);
byte packing lives in `src/bytes.h`.

## Active HD artwork workstream, 2026-10-17

The user activated Zune HD Now Playing artist backgrounds after the spoken-media
release. Use `feature/hd-now-playing-art`; start with read-only descriptor and
identity evidence. PPP capture evidence is in `docs/ZUNE_VENDOR_OPS.md` and
earlier catalog-route leads in `docs/SPOKEN_MEDIA_RESEARCH.md`. Preserve existing
artist IDs and serialized device ownership. Advertised properties, a successful
USB exchange and physical image presentation are separate gates. The spoken
workstream below describes the preserved implementation baseline.

October 18: the fully tagged fresh-artist experiment delivered two acknowledged
JPEG responses over the native USB responder, including the device-background
route (3 DNS queries, 9 HTTP requests). This is transport evidence, not visual
acceptance or a safe existing-artist retrofit. The startup stream parser passes
179 checks for fragmented/coalesced/repeated CLIENT greetings before PPP.
See `docs/HD_ARTWORK_RESEARCH.md` for journals, cleanup and remaining gates.
The first-sync/recovery commands retain experimental CLI tooling. October 19
extracts the responder into shared `src/hd/` and adds a public C API in
`src/hd_artwork.c`. Read `docs/HD_ARTWORK_API.md` for ownership, callback ordering,
identity refusal, partial delivery and the remaining app hardware gate.
October 19: the responder has sixteen bounded TCP slots and separate background,
six-image gallery and thumbnail roles. `--biography utf8.txt` now supplies bounded
plain text through escaped Atom HTML, with independent full-response ACKs.
The existing Disturbed refresh received no biography request. After the user
deleted the album on the HD and restarted, a fresh import with the same catalog
GUID received two biography requests/two complete ACKs and all eight image
assets. Separate full album proof and reindex handoff passed, and the user
confirmed the biography works on the HD. Persistence and existing-artist
replacement remain separate gates; do not automatically delete/recreate artists. See the dated
research and transport walkthrough for the initial final-proof failure and
subsequent successful verification.
The subsequent full-album test retained 16 Disturbed tracks with exact payload,
cover and ordered-reference proof, preserved the original inventory and confirmed
one reindex handoff. Three distinct artist JPEGs were acknowledged over USB;
visible rotation/persistence remain separate. See the transport walkthrough and
dated full-album evidence before extending this experimental mechanism.

## Active spoken-media workstream

Use `feature/podcasts-audiobooks` for this private work. Read
[the usage guide](docs/SPOKEN_MEDIA.md), [research evidence](docs/SPOKEN_MEDIA_RESEARCH.md)
and [TOC](docs/TOC.md) first. Podcast audio and visible show covers are confirmed
by the user on Classic / Keel; resume and other models remain separate gates.
Experimental native audiobook lookup/send APIs now support Keel/Pavo/Draco. Small
two-part MP3 tests verified schema 0x12, metadata, exact audio and JPEG readback
on Classic, HD and Draco; the user accepted the initial Classic test. Draco
podcast state reads Classic v2 with exact live identity correlation. Firmware
resume, chapter mapping, Draco completed-podcast cleanup and HD/Draco
screen/playback acceptance remain separate. Use wildcard inventory for
audiobooks: HD MP3-filtered enumeration omits these objects. Preserve serialized
device ownership, exact spoken identities and the no-blind-retry contract.
Feeds, downloads and local state belong in the app. The historical HD background
deferral was superseded by the October 17 workstream above.

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
- **Linux build:** libavformat/libavutil for native metadata probing
- **Runtime helpers:** ffmpeg/ffprobe/sips CLIs where used, run without a shell via `zune_run_tool`. Lookup: `ZUNE_FFMPEG`/`ZUNE_FFPROBE`/`ZUNE_SIPS`, then PATH, then (macOS only) Homebrew and /usr/local. See README.md.

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
- Logging: `ZLOG(ZUNE_CHATTER_<LEVEL>, "tag", "message", ...)` from `wiretap.h`
  (no trailing newline): WHISPER protocol detail/per-item progress, TALK
  lifecycle, SHOUT retries/fallbacks, SCREAM failures. Never write stderr
  directly. Status functions return
  `ZUNE_OK`/`ZUNE_BLOWN`; others return an ID or 0, or a pointer or NULL (see
  the return-convention table in docs/API_REFERENCE.md).
- Thread-local error string via `zune_set_error()` / `zune_get_error()`. A
  public function that can fail starts with `zune_clear_error()`; one that
  cannot fail never clears it (`tests/run-error-convention.sh`).

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

## Current API documentation

Read docs/API_REFERENCE.md for every declaration and its current ownership/return-value limits. Public declarations carry `ZUNE_API`; the build uses `-fvisibility=hidden`, and `tests/run-symbols.sh` keeps the export list equal to zune.h. Never sever while a worker still uses the device. Keep a returned scan allocated while calling find helpers; free_scan does not clear the borrowed device cache. Regenerate/check both API_REFERENCE.md and TOC.md after interface changes.

## Important Patterns

### Split Header/Data Mode
The Zune requires PTP data containers to be sent as TWO separate USB transfers: 12-byte header first, then payload. Auto-detected during GetDeviceInfo. Without this, all MTP operations return 0x2002.

### SendObjectPropList (0x9808)
The Zune requires album/artist objects to be created atomically with all metadata via SendObjectPropList. Post-creation SetObjectPropValue for AlbumArtist returns 0xA801.

### MTPZ Authentication
Required before any write operations. Uses RSA/AES handshake via libgcrypt. Optional local keys in `mtpz_keys.h` with `~/.mtpz-data` override. Public source omits populated values. A handle may be returned even after authentication failure.

### Embedded Keys
A locally supplied `mtpz_keys.h` enables embedded MTPZ data. The header is optional in public builds; users can instead provide `~/.mtpz-data`. See docs/BUILD_WITH_MTPZ.md.

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

- All logs go through `zune_log` to the installed wiretap or stderr as `[prefix] message`; `ZUNE_CHATTER=whisper` shows debug detail
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
- **Document dependencies accurately**: the library links system libraries and some helpers invoke command-line tools. The consuming app is responsible for packaging them.

### Skills & Domain Knowledge
- C systems programming — kernel-level, driver development, USB subsystems
- macOS DriverKit — USB driver extensions, IOKit, entitlements, code signing
- MTP/PTP protocols — device communication, vendor extensions, MTPZ auth
- FFmpeg — transcoding pipelines, codec internals, container formats (migrating to C API)

## Documentation Map

See `docs/TOC.md` for the master function index.
