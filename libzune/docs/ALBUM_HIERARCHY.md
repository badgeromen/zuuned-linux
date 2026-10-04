# Zune MTP Album/Artist Object Hierarchy

> **Documentation status (2026-10-04):** Historical hierarchy notes. The current artist/album path uses SendObjectPropList and a zero-byte SendObject commit, not the SendObjectInfo recipe below. Artist reuse is now implemented. Use API_REFERENCE.md, ARTIST_REUSE.md and WIRE_CAPTURE_FINDINGS.md for current integration and later evidence.

## Why Album Objects Matter

The Zune firmware reads album and artist metadata from **MTP abstract objects**, NOT from individual track properties. If you send a track with `artist="Breaking Benjamin"` and `album="Dark Before Dawn"` set as track metadata, the Zune will display "Unknown Album" / "Unknown Artist" unless corresponding MTP abstract objects exist.

This is documented in libmtp: "The Toshiba Gigabeat S (and probably its sibling the Microsoft Zune) will only display album information tags for a song in case there is also an abstract album created with the album interface with the exact same name."

## Object Types

### Artist Object (Format 0xB218)
- Empty file (0 bytes)
- Filename: `{ArtistName}.art`
- Name property (0xDC44) set to artist name
- Created via `SendObjectInfo` + `SendObject(empty)`

### Album Object (Format 0xBA03 — AbstractAudioAlbum)
- Empty file (0 bytes)
- Filename: `{Artist}--{Album}.alb`
- Properties:
  - Name (0xDC44): album title
  - Artist (0xDC46): artist name
  - Genre (0xDC8C): genre string
  - ArtistId (0xDAB9): handle of linked Artist object
  - RepresentativeSampleData (0xDC86): album art JPEG bytes
  - RepresentativeSampleFormat (0xDC81): 0x3801 (JPEG)

### Track-to-Album Linking
- `SetObjectReferences(albumHandle, [trackHandle1, trackHandle2, ...])` — links tracks to album
- Each track also gets `ArtistId` (0xDAB9) set to the artist handle

## Creation Flow (After Sending Tracks)

```
1. Send all tracks → collect item_ids grouped by artist/album

2. Create Artist objects:
   for each unique artist:
     SendObjectInfo(format=0xB218, filename="{artist}.art", size=0)
     SendObject(empty)
     SetObjectPropValue(handle, 0xDC44, artistName)
     → store artistHandle

3. Create Album objects:
   for each unique album:
     a. Check if album already exists on device (merge case)
     b. If new: LIBMTP_Create_New_Album(name, artist, genre, trackIds)
     c. If existing: LIBMTP_Update_Album(albumId, mergedTrackIds)
     → store albumId

4. Link Album → Artist:
   SetObjectPropValue(albumId, 0xDAB9, artistHandle)

5. Link each Track → Artist:
   for each trackId in album:
     SetObjectPropValue(trackId, 0xDAB9, artistHandle)

6. Send Album Art:
   LIBMTP_Send_Representative_Sample(albumId, jpegData, width=200, height=200)
```

## Timing Requirements
- 500ms pause after album creation (device needs time to process)
- 300ms pause between albums (let device flush)
- 200ms pause between track sends (from Linux reference)

## Merge Logic

When sending tracks to an album that already exists on the device:
1. Fetch existing albums: `LIBMTP_Get_Album_List()`
2. Match by `"{artist}\t{album}"` key
3. If found: merge old trackIds + new trackIds → `LIBMTP_Update_Album()`
4. If not found: create new album

## libzune API

```c
// Create artist object
uint32_t zune_create_artist_object(dev, "Breaking Benjamin");

// Create album with track IDs
uint32_t albumId = zune_create_album_object(dev, "Dark Before Dawn", "Breaking Benjamin", "Rock", trackIds, 12);

// Link album and tracks to artist
zune_set_artist_id(dev, albumId, artistHandle);
for (int i = 0; i < 12; i++)
    zune_set_artist_id(dev, trackIds[i], artistHandle);

// Set album art
zune_set_album_art(dev, albumId, jpegData, jpegLen);
```

## References
- zune-explorer `zune-manager.js` `_createAlbumObjects()` (lines 496-637)
- libmtp documentation on Toshiba Gigabeat S (Zune hardware sibling)
- MTP specification: AbstractAudioAlbum (0xBA03)
