# Building a sync pipeline with libzune

This is a current integration guide, not an application queue implementation.
Applications own preparation, ordering, duplicate policy, recovery and progress
presentation. libzune calls are synchronous; serialize device operations.

## Music

1. Inspect the original file with `zune_probe`. Preserve source tags and user
   choices in your application; the library does not infer online album editions.
2. Prepare a format supported by the target device. Upload functions do not
   transcode. The library's legacy audio/video arm helpers currently hard-code
   Homebrew's FFmpeg path; Linux applications should supply their own preparation
   pipeline. Identify content accurately rather than trusting a filename alone.
3. Refresh storage with `zune_refresh_storage` and inspect failures. Cached
   free-space getters alone do not reflect preceding uploads.
4. Upload with `zune_smuggle_track`, `zune_smuggle_track_tagged`, or
   `zune_smuggle_track_ex` for a progress callback. Check return and returned ID
   before linking or verifying anything. Progress callbacks run on the caller's
   transfer thread; dispatch GUI updates yourself.
5. Obtain artist objects through `zune_forge_artist`, which reuses matching live
   objects. Collect successful track IDs by the application's album identity.
6. Create an album using `zune_forge_album`, or read existing references and pass
   the complete desired list to `zune_rewire_album`. Rewire does not merge for
   you. Link artist IDs with `zune_link_artist` and apply JPEG art with `zune_brand`.
7. Read back properties with `zune_verify` where appropriate. This compares the
   requested title/artist/size, not audio content or successful physical playback.

Name-based `zune_find_track` is not a universal duplicate detector. It omits disc,
track number, content fingerprint and edition identifiers. A scan is a snapshot
and is not amended by uploads. Keep its allocation alive for find calls.

## Video

Prepare compatible media separately. Prefer `zune_smuggle_video_named`:
ObjectFileName includes the transport extension, Name is the desired display
title, and TV series/season/episode occupy independent vendor properties.

- 0: upload and requested metadata/art completed.
- `ZUNE_VIDEO_METADATA_INCOMPLETE` (1): media exists; use its ID for targeted
  repair. Do not duplicate the upload.
- -1: input/file-transfer failure. Inspect diagnostic/readback evidence before
  retrying; failure does not guarantee the device has no partial object.

Older category-specific helpers remain public but do not report ancillary
metadata failures. See [VIDEO_TITLES.md](VIDEO_TITLES.md).

## Photos and playlists

`zune_smuggle_photo` prepares a JPEG automatically and can use a named folder.
Its Linux preparation requires `ffmpeg` on PATH; macOS uses `sips`. This is a
flat device folder operation, not Zuuned Linux's virtual nested-album database.
The function does not return an uploaded object ID.

`zune_forge_playlist` creates an object referencing already-uploaded track IDs.
`zune_rewire_playlist` supplies replacement order/membership. Deleting a playlist
removes its object rather than its media. Verify actual device presentation and
persistence after eject/re-index for the models you support.

## Completion and interruption

`zune_sync_notify` reports a 1-based item index and percentage before/after the
item (0..100); op_kind 0 is host-to-device upload. It is best-effort UI feedback,
not completion evidence.

After writes, call `zune_finalize` to request re-indexing, then disconnect when
all operations have finished. Its current 0 result does not certify each vendor
request succeeded. Some models reject 0x9201/0x9202; inspect logs and actual
post-reindex content.

For cancellation, request `zune_abort`, await return, inspect any interrupted
object and decide recovery. Do not kill or sever a session during transfer.
Use `zune_autopsy` immediately after failures; unsupported operations and full
storage should not be treated as generic USB retry problems. `zune_unjam` clears
endpoints but does not roll back an object or guarantee a safe retry.

For ownership, exact signatures and return-value exceptions, use the
[API reference](API_REFERENCE.md). Historical wire evidence is kept separately in
[WIRE_CAPTURE_FINDINGS.md](WIRE_CAPTURE_FINDINGS.md).
