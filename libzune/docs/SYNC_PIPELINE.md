# Sync Pipeline — How Content Gets to the Zune

## Music Sync Pipeline

```
Source file (any format)
│
├── Is it MP3? ──→ Retag to ID3v2.3 (ffmpeg -c copy, instant)
├── Is it WMA? ──→ Send directly (native Zune format)
└── Other (FLAC/M4A/OGG/WAV/AAC)?
    └── Transcode to MP3 320kbps CBR (ffmpeg -codec:a libmp3lame)
        └── ID3v2.3 tags included automatically
│
├── Send file: zune_send_track_with_meta()
│   ├── Sets: title, artist, album, genre, tracknumber, duration
│   ├── Sets: wavecodec (0x0055 MP3 / 0x0161 WMA)
│   ├── Sets: samplerate=44100, nochannels=2, bitrate=320000
│   ├── 3 retry attempts with USB stall recovery
│   └── Returns: item_id (MTP object handle)
│
├── Collect item_id into per-album bucket
│   Key: "{artist}\t{album}"
│   Bucket: { artist, album, genre, trackIds[] }
│
└── After all tracks sent:
    ├── Create Artist objects (format 0xB218)
    ├── Create/update Album objects (format 0xBA03)
    ├── Link albums + tracks to artists (ArtistId 0xDAB9)
    └── Send album art (RepresentativeSampleData)

Clean up temp files → Show "disconnect to apply changes"
```

## Video Sync Pipeline

```
Source video file
│
├── Detect profile from target Zune model:
│   ├── Zune 30:     WMV2 320x240, 800k video, 128k WMA audio
│   ├── Zune 80/120: H.264 Baseline L2.1, 320x240, 768k, 128k AAC
│   ├── Zune HD:     H.264 Baseline L3.1, 480x272, 2500k, 192k AAC
│   └── Zune HD 720p: H.264 Baseline L3.1, 1280x720, 8000k, 192k AAC
│
├── Transcode (ffmpeg with profile-specific args)
│
├── Determine content type:
│   ├── TV Episode? → zune_smuggle_video_named(..., ZUNE_METAGENRE_TV_SHOW, ...)
│   │   ├── MetaGenre = 0x26 (TV Show)
│   │   ├── Name = episode title (e.g. "Let You Down")
│   │   ├── ObjectFileName = transport filename with extension, separate from Name
│   │   ├── Vendor props: 0xDA9A (series), 0xDAB5 (season), 0xDAB6 (episode)
│   │   └── Optional: description (UCS-2), poster JPEG
│   │
│   ├── Music Video? → zune_smuggle_video_named(..., ZUNE_METAGENRE_MUSIC_VIDEO, ...)
│   │   ├── MetaGenre = 0x23 (Music Video)
│   │   └── Optional: poster JPEG
│   │
│   └── Movie? → zune_smuggle_video_named(..., ZUNE_METAGENRE_MOVIE, ...)
│       ├── MetaGenre = 0x25 (Movie)
│       ├── Name = movie title, without filename extension
│       ├── Optional: description, poster JPEG
│       └── TMDB poster attached as representative sample
│
└── Clean up transcoded temp file
```

The named API is opt-in; legacy wrappers retain their former title behavior.
An upload completed with metadata failure returns `+1` and its actual object
ID, so callers must report the warning without resending the media. See
[VIDEO_TITLES.md](VIDEO_TITLES.md) for the exact wire split and verification.

## Photo Sync Pipeline

```
Source image (any format)
│
├── Preprocess: zune_prepare_photo()
│   ├── macOS: sips -Z 480 -s format jpeg -s formatOptions 90
│   └── Linux: ffmpeg scale to 480px max, JPEG output
│   Result: baseline JPEG, max 480px longest edge, sRGB
│
├── Send: zune_send_photo(dev, filepath, album_name)
│   ├── Creates album folder if needed
│   ├── Sets filetype = LIBMTP_FILETYPE_JPEG
│   ├── 3 retry attempts with stall recovery
│   └── Photo viewable after Zune re-indexes on disconnect
│
└── Clean up preprocessed temp file
```

## Playlist Sync Pipeline

```
Playlist data (name + track IDs)
│
├── New playlist → zune_create_playlist(dev, name, track_ids, count)
│   └── Creates AbstractAudioVideoPlaylist object (0xBA05)
│
└── Update existing → zune_update_playlist(dev, id, name, track_ids, count)
    └── Replaces track list
```

## Bidirectional Pull Pipeline

```
Device content → Local library
│
├── Track: zune_download_track(dev, item_id, dest_path)
│   └── Saves to ~/Music/Zuuned Imports/{Artist}/{Album}/{filename}
│
├── Video: zune_download_video(dev, item_id, dest_path)
│   └── Saves to ~/Movies/Zuuned Imports/{filename}
│
├── Photo: zune_download_photo(dev, item_id, dest_path)
│   └── Saves to ~/Pictures/Zuuned Imports/{AlbumName}/{filename}
│
└── Filename sanitization: replace /\:?*"<>| with underscore
```

## Deduplication

Before syncing, existing device tracks are indexed:
```swift
var existing = Set<String>()
for dt in deviceTracks {
    let key = "\(dt.artist.lowercased())\t\(dt.album.lowercased())\t\(dt.title.lowercased())"
    existing.insert(key)
}
```

Tracks matching an existing key are skipped with `[sync] skipping duplicate`.

## Sync Close-Out — REQUIRED (2026-08-29, verified on Keel)

A sync is not finished when the last byte lands. Leaving the MTP
session open after the final transfer parks the device in sync mode
**forever** — screen frozen at the last notify percentage, no DB
re-index, and (observed on Keel) media playback can misbehave until
the rebuild happens. The full close-out sequence, in order, on the
device thread:

1. **Terminal 0x922A** at 100/100, name "Sync complete". Notifies are
   PRE-transfer: the device only learns item N finished from item
   N+1's notify, so without this extra one its screen parks at
   (N-1)/N (a stuck 95%). See ZUNE_VENDOR_OPS.md working recipe.
2. **Storage refresh** (GetStorageInfo) — last read while the session
   is still open.
3. **Finalize vendor trio** — ReportAddedDeleted (0x9201),
   CleanDataStore (0x9108), ReportAcquired (0x9202). 0x9201/0x9202
   return 0x2005 on Keel — expected, ignore.
4. **CloseSession + release the USB interface** (zune_sever). THIS is
   the trigger: the device exits sync mode, restarts, and re-indexes
   its database (the LIBMTP_Release_Device finding from the macOS
   quirks doc). It then re-enumerates on USB; the app's normal
   arrival path reconnects with the fresh library.

Implementations: ZuunedLinux `SyncEngine::finishSync` →
`DeviceService::finalizeAndRelease()` (eject minus the queue-clearing
signal). **Mac backport pending**: ZuunedMac shows "disconnect your
Zune to apply changes" and relies on the user unplugging — it should
adopt the same automatic close-out (and the terminal notify, once
sync notifies are backported).
