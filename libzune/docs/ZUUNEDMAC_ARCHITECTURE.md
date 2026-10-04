# ZuunedMac — SwiftUI Architecture

## Overview

ZuunedMac is a native macOS app for managing Microsoft Zune media players. It's the first macOS Zune sync application — the Windows Zune Software never ran on Mac, and no third-party tool provided full sync capabilities until this project.

Built with SwiftUI, backed by libzune (C library) for all USB/MTP communication.

## Stack

```
┌─────────────────────────────────────────┐
│           SwiftUI Views                  │
│  (ContentView, DevicePanelView, etc.)   │
├─────────────────────────────────────────┤
│           Services Layer                 │
│  MTPService (actor) — thin Swift wrapper│
│  SyncManager — orchestrates sync flow   │
│  AlbumArtService — disk cache           │
│  TranscodeService — ffmpeg wrapper      │
│  USBWatcher — IOKit hotplug             │
├─────────────────────────────────────────┤
│     libzune (C library, .dylib)         │
│  zune_connect/disconnect/scan/send/...  │
├─────────────────────────────────────────┤
│  vendored libmtp (with raw PTP patches) │
├─────────────────────────────────────────┤
│          libusb → macOS USB stack        │
└─────────────────────────────────────────┘
```

## Key Design Decisions

### Actor-based MTPService
All USB communication goes through a Swift actor (`MTPService`). This ensures thread safety — MTP is single-threaded and concurrent access causes corruption.

### libzune Submodule
libzune is a git submodule at `zuuned/libzune/`. It's compiled separately (`make`) and the resulting `libzune.dylib` is linked into the Xcode project. This separation allows:
- libzune to be used by other projects (CLI tools, other GUI frameworks)
- C library testing independent of Swift
- Clear boundary between protocol logic (C) and UI logic (Swift)

### Environment-Driven UI
Device connection state propagates via SwiftUI `@Environment`:
- `zuneSprayIntensity` — graffiti spray paint intensity
- `zuneDeviceWarmth` — device glow warmth
- `zuneDeviceColor` — genre-based mood color
- `zuneDeviceGrungy` — grunge mask toggle

When a Zune connects, the UI literally "grimes up" — rougher edges, oil smears, spray paint atmosphere.

### Sync Flow
```
User queues tracks/photos/videos
  → SyncManager.startSync()
    → For each track:
        1. Transcode if needed (ffmpeg → MP3 320k)
        2. Retag to ID3v2.3 if MP3
        3. Send via zune_send_track_with_meta()
        4. Collect item_id for album creation
    → Create artist objects (zune_create_artist_object)
    → Create album objects (zune_create_album_object)
    → Link tracks to artists (zune_set_artist_id)
    → Send album art
    → For each photo: zune_send_photo() (auto resize+convert)
    → For each video: zune_send_movie/tv_episode/music_video()
    → Finalize (vendor operations)
    → "Disconnect your Zune to apply changes"
```

### Device Library Loading (ZuneDB Unified Scan)
```
Device connects
  → zune_db_scan() — single USB operation via vendor opcode 0x1792
    → Discovery-based descriptor mapping (all 96 descriptors)
    → Tracks, videos (with MetaGenre), photos, albums, artists, photo albums
    → Playlists (internal MTP fallback until ZMDB format fully decoded)
  → Fetch album art thumbnails (MTP — binary image data)
  → Fetch video poster thumbnails (MTP — binary image data)
  → Fetch photo thumbnails (MTP — binary image data)
  → UI populated
```

## File Structure

```
ZuunedMac/
├── App/
│   ├── AppState.swift          — @MainActor state management
│   └── ZuunedApp.swift         — App entry point
├── Models/
│   ├── ZuneDeviceInfo.swift    — Device metadata
│   ├── ZuneTrackInfo.swift     — Track model
│   ├── ZuneFile.swift          — Video/photo file model
│   ├── ZunePlaylistInfo.swift  — Playlist model
│   └── SyncQueueEntry.swift    — Sync queue item
├── Services/
│   ├── MTPService.swift        — libzune C API wrapper (actor)
│   ├── SyncManager.swift       — Sync orchestration
│   ├── SyncQueueService.swift  — Queue management
│   ├── AlbumArtService.swift   — Art cache (actor)
│   ├── TranscodeService.swift  — ffmpeg wrapper (actor)
│   ├── USBWatcher.swift        — IOKit USB hotplug
│   ├── LibraryService.swift    — Local library DB (SQLite)
│   ├── PlayerService.swift     — AVPlayer wrapper
│   └── TMDBService.swift       — Movie metadata API
├── Views/
│   ├── ContentView.swift       — Main layout (sidebar + content + device panel)
│   ├── Library/                — Music/video/photo library views
│   ├── Device/                 — Device content browsers
│   ├── Navigation/             — Sidebar, DevicePanelView
│   ├── Player/                 — Player bar, video player
│   └── Dialogs/                — Edit, import, lookup sheets
├── ViewComponents/             — Reusable UI components
│   ├── GlassGrungeView.swift   — Glass panel with grunge edges
│   ├── SprayGroup.swift        — Graffiti spray background
│   ├── DevicePresence.swift    — Connection bloom animations
│   └── PivotBar.swift          — Zune-style tab bar
├── Theme/
│   ├── ZuneColors.swift        — Color tokens + mood palette
│   ├── ZuneFonts.swift         — Typography scale
│   └── ZuneStyles.swift        — Glass modifiers, button styles
└── Bridge/
    └── Zuuned-Bridging-Header.h — C interop (zune.h + libmtp.h)
```

## Unique Features (Not in Any Other Zune App)

1. **Video sync with MetaGenre** — movies, TV shows, music videos properly categorized
2. **TV series metadata** — season/episode via vendor properties (0xDA9A/0xDAB5/0xDAB6)
3. **Photo album folders** — organized by album name on device
4. **Photo resize/convert** — auto 480px JPEG for Zune compatibility
5. **Genre-based mood colors** — device personality bleeds into UI
6. **Glass + grunge design system** — clean panels that "grime up" when device connects
7. **ZMDB fast scan** — instant library enumeration via vendor opcode 0x1792
8. **IOKit USB auto-detection** — instant connect/disconnect response
9. **Bidirectional sync** — push AND pull for all media types
