# libzune API reference

Current public interface: **114 functions** in [include/zune.h](../include/zune.h).
The base review is October 4, 2026, with spoken-media contracts reviewed October 14.
The exact declarations below are generated; [TOC.md](TOC.md) links each one to
its implementation. Build and platform requirements are in [README](../README.md).

## Themed verbs

Public names follow the Zune Rebellion lexicon: `breach`/`sever` connect and
disconnect, `smuggle` sends media, `extract` copies it off, `forge` creates an
object, `rewire` updates references, `brand` sets artwork, `purge` deletes,
`infiltrate` reads the device database, `autopsy` explains a failure, `unjam`
clears USB stalls, `arm` prepares a file, `snap`/`grab` make and fetch
thumbnails, `probe` reads metadata, `abort` cancels, `dossier` reports the
library version, `wiretap` installs a log callback, `set_chatter` sets how
much is logged (WHISPER, TALK, SHOUT, SCREAM or SILENT) and `go_dark` wipes
and frees the loaded MTPZ keys at process teardown. Plain `get_*`/`free_*`
names read and release data.

**Logging:** messages are `[tag] text` lines on stderr by default, at TALK and
above (or the `ZUNE_CHATTER` environment level). `zune_wiretap` and
`zune_set_chatter` are process-wide; set them before opening a device. The
callback may run on several threads at once. MTPZ authentication messages
use the `[mtpz]` tag; their certificate hex dumps are WHISPER only.

Every public declaration is marked `ZUNE_API`. The library is built with
`-fvisibility=hidden`, so the shared object exports exactly these functions
(`tests/run-symbols.sh`). On Linux it is `libzune.so.0.3.0` with SONAME
`libzune.so.0`; `zune_dossier()` reports the running version, while the
`LIBZUNE_VERSION_*` macros describe the header you compiled against.

## Connection, threads and ownership

Calls block. Serialize operations for a device and dispatch them from a worker,
not a GUI thread. The library does not own your worker thread. `zune_abort`
requests cooperative cancellation; wait for the active call to return before
clearing cancellation or tearing down. **Do not call `zune_sever` while another
operation uses the handle.** Its short delay is not a worker join. Abrupt termination can leave an incomplete device object.

`zune_breach` opens a session and attempts authentication when data is available.
It can return a handle even when authentication fails or no data is available.
It does not scan the library. `zune_is_live` checks local handle/backend flags,
not USB responsiveness. See [BUILD_WITH_MTPZ.md](BUILD_WITH_MTPZ.md).

Device name/model/serial pointers are borrowed and valid until disconnect.
Enumeration results belong to the caller and use their matching free function.
Do not separately free records within a `ZuneDBLibrary`; `zune_free_scan` owns
all of them. Plain allocated outputs (object names, references, series/title
strings) use `free`. Prepared-media paths require both `unlink` and `free`.

**Scan cache lifetime:** a successful `zune_infiltrate` stores a borrowed pointer
to the returned scan in the device. Keep that scan alive for the scan-based music `zune_find_*`
calls. `zune_find_podcast_episode` and `zune_find_audiobook_part` instead query live MTP state and require
no ZMDB scan. `zune_free_scan` does not clear the device pointer. After freeing it, do
not call a scan-based find helper until another successful scan replaces the cache. A
failed scan does not replace it. The simplest lifetime is scan, search/use,
sever, free scan. Scans are snapshots, not a live index of subsequent uploads.

## Return values and diagnostics

Functions that return a status use `ZUNE_OK` (0) for success and `ZUNE_BLOWN`
(-1) for failure. The [return-convention table](#return-conventions) gives every
function's convention; the others are:

- Most forge functions return a nonzero object ID, or 0 on failure.
  `zune_forge_podcast_series` instead returns a status and writes the ID through
  an output pointer; on failure, a nonzero ID identifies an orphan requiring review.
  Both podcast mutation APIs return `ZUNE_PODCAST_FAILED_CLEANED` (-2) only
  after confirmed deletion of their new object; other failures return `ZUNE_BLOWN`.
  A zero ID on its own never proves cleanup.
- `zune_get_folders` returns a count or `ZUNE_BLOWN`. Legacy music/video/photo/playlist
  enumeration can return NULL for empty or failure and may skip unreadable rows.
  Spoken inventories, source maps and listening-state APIs instead return `ZUNE_OK` for
  complete success (including known empty) or `ZUNE_BLOWN` with NULL/count zero on failure.
  They do not return a partial result. Use each function’s documented contract.
- Boolean helpers and `zune_decode_filename` return 1/0.
- `zune_smuggle_video_named` returns 1 (`ZUNE_VIDEO_METADATA_INCOMPLETE`) when
  media is uploaded but requested metadata/art was not fully saved. Its returned
  object ID is valid: repair that object, **do not upload it again**.
- `zune_finalize` currently returns `ZUNE_OK` for a non-NULL device even if its vendor
  operations fail. It is a best-effort re-index request, not proof of persistence.
- `zune_get_track_state` succeeds if at least one requested playcount/rating
  property was read. Failed fields become zero; skip count is always zero in
  this MTP path. Use the scan's ZMDB fields when available.

**Errors.** Every call that can fail clears the thread-local error on entry, so
`zune_get_error` describes the most recent such call on the calling thread; NULL
means that call recorded no message. Calls that cannot fail (`zune_autopsy`,
`zune_autopsy_name`, the `free_*` releasers, the cached device getters,
`zune_abort`, `zune_sever` and the log settings) leave it unchanged, so a caller
can release partial results or read the response code before reading the
message. Read it before the next call that can fail. Some failure paths only
log, so a failure can leave it NULL.
`zune_autopsy` exposes the last PTP response, not an immutable error history:
0x2001 is OK, 0x2005 unsupported operation, 0x200C full storage, 0x02FF transport
failure, and 0 means no response. Cleanup or later calls can replace it.

**Deprecated.** `zune_infiltrate_legacy` and the legacy video helpers
`zune_smuggle_movie`, `zune_smuggle_episode`, `zune_smuggle_clip` and
`zune_smuggle_other` are marked `ZUNE_DEPRECATED`: GCC and Clang warn at each
call and name the replacement. They still work and keep their contracts.

## Read-only quick start

```c
#include "zune.h"
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) {
        fprintf(stderr, "Breach failed: %s\n", zune_get_error());
        return 1;
    }
    const char *name = zune_get_name(dev);
    printf("Connected: %s, free: %" PRIu64 " bytes\n",
           name ? name : "Zune", zune_get_headroom(dev));
    ZuneDBLibrary *library = NULL;
    int result = zune_infiltrate(dev, &library);
    if (result == 0) {
        printf("%d tracks, %d videos, %d photos\n",
               library->track_count, library->video_count, library->photo_count);
    } else {
        fprintf(stderr, "Scan failed: %s (PTP 0x%04x)\n",
                zune_get_error(), (unsigned)zune_autopsy(dev));
    }
    zune_sever(dev);
    zune_free_scan(library);
    return result == 0 ? 0 : 1;
}
```

Save as `example.c` at the repository root, build the library, then on Linux:

```bash
cc -std=c99 -Wall -Wextra -Werror -Iinclude example.c ./libzune.a \
  $(pkg-config --libs libusb-1.0 libgcrypt libavformat libavutil) -o example
```

This reads device information and library data; it does not transfer or delete.

## Choosing operations

**Scanning:** `zune_infiltrate` reads ZMDB and includes an internal MTP playlist
fallback when no playlists were decoded. It is not universally one USB operation
or a complete read of every property. Classic media sizes and some user-state
fields can be unavailable. Artwork is fetched separately. The legacy scan API
returns only tracks/videos/photos; new clients should use `ZuneDBLibrary`.

**Duplicates:** `zune_find_track` compares title, artist and album with
`strcasecmp`; it does not compare disc, track number, file content or edition
IDs. Video/photo lookup compares the legacy `filename` field. For ZMDB videos
that field is a display title, not a guaranteed transport filename. These are
simple search helpers, not a safe universal dedup policy. Applications must
resolve ambiguity and preserve album editions themselves.

**Uploads:** track/video uploads send prepared compatible media; they do not
transcode automatically. Music display also requires album/artist abstract
objects and references. `zune_forge_artist` reuses a live matching artist and
refuses creation after an incomplete inventory read; it does not remove existing
duplicates. `zune_rewire_album` sets the supplied references, not an automatic
union with existing membership. Compose the desired complete list first.
Refresh storage before transfers. The transfer stack cannot send objects larger
than its 32-bit compressed-size limit. A failed or cancelled upload needs
inspection before retry; do not infer that no partial object exists.

**Video:** prefer `zune_smuggle_video_named` for independently controlled display
title and transport filename, with explicit partial-metadata status. The older
movie/episode/clip/other APIs remain compatible but ignore metadata/art failures
after upload; the episode helper synthesizes a series/episode display title.
See [VIDEO_TITLES.md](VIDEO_TITLES.md).

**Preparation:** audio/video arm helpers and `zune_retag` run `ffmpeg`;
`zune_arm_photo` uses `sips` on macOS and `ffmpeg` on Linux. Tools run without a
shell and are located through `ZUNE_FFMPEG` / `ZUNE_FFPROBE` / `ZUNE_SIPS` (a path,
or a name to search for), then `PATH`, then on macOS `/opt/homebrew/bin` and
`/usr/local/bin`. A missing tool fails with an error naming its override.
`zune_smuggle_photo` invokes that preparation automatically. Thumbnail utilities
also require external tools. Each prepared output is a new mode-0600 file under
`$TMPDIR` (default `/tmp`) with a unique name, so concurrent preparations of the
same input do not collide; failed or empty outputs are removed before returning.
`USE_LIBAV` probing is in-process; the fallback invokes `ffprobe`.

## Data types

Exact layouts and enums live in [zune.h](../include/zune.h). Rebuild consumers
when these structs change; this project does not promise a stable binary ABI.

| Type | Purpose and important fields |
|---|---|
| `ZuneDeviceHandle` | Opaque device/session handle. |
| `ZuneChatter` | Log level: WHISPER debug, TALK info (default), SHOUT warning, SCREAM error, SILENT nothing. |
| `ZuneWiretapFn` | Log callback `(level, tag, message, ctx)`; tag and message are valid only during the call. |
| `ZuneModel` | Video profile family: 30, 80/flash, HD, HD 720p, unknown. |
| `ZuneDeviceFamily` | Keel 0x00, Scorpius 0x02, Draco 0x03, Pavo 0x06, unknown 0xFF. |
| `ZuneTrack` | ID, text metadata, duration/size, track/disc, playcount/rating/skip count/last played. Zero can mean unavailable; last played uses Windows FILETIME. |
| `ZuneVideoFile` | ID, legacy filename, size, metagenre, parent, explicit title, object_filename and object_format. ZMDB object_filename can be empty. |
| `ZunePhotoFile` | ID, filename, size and parent ID. |
| `ZunePhotoAlbum` | Folder ID, name and photo count. |
| `ZunePlaylist` | ID, name, ordered track IDs and count. |
| `ZuneAudiobookPartInput` | Borrowed book content key/name/author, part title/order and duration; caller preserves immutable prepared audio. |
| `ZunePodcastSeries` | Owned show ID/name/author/feed URL, released with `zune_free_podcast_series`. |
| `ZunePodcastEpisodeInput` | Borrowed prepared-episode metadata and live series ID; keep strings/file unchanged through the synchronous send. |
| `ZuneAlbumObject` | ID, name, artist, track IDs and count. |
| `ZuneDBAlbum` | ID, title, artist, year (HD; zero on Classic). |
| `ZuneDBArtist` | ID and name, not a portrait or online provider identity. |
| `ZuneDBPhotoAlbum` | ID and name. |
| `ZuneDBLibrary` | Arrays/counts for all seven media/object categories, is_hd and scan_ms. |
| `ZuneLibrary` | Deprecated tracks/videos/photos-only scan result. |
| `ZuneMetadata` | title, artist, albumartist, album, genre, tracknumber, duration_ms, discnumber and year. Missing text is NULL, missing numbers zero. |
| `ZuneFolderEntry` | ID and allocated name. |
| `ZuneMetaGenre` | Other 0x21, music video 0x23, movie 0x25, TV 0x26. |
| `zune_progress_fn` | `(uint64_t bytes_sent, uint64_t bytes_total, void *userdata)`. Runs in the synchronous transfer call; do not reenter device I/O. |

See [Spoken-media integration](SPOKEN_MEDIA.md) for podcast call order, covers,
identity persistence, failure recovery and the separate audiobook research gate.

## Maintaining this reference

Run `node tools/update-api-reference.mjs` after changing the public header and
review the operation notes against implementation changes. Use `--check` to
validate generated signatures and one note per declared function. Then run
`node tools/update-toc.mjs`. No authentication data is read by either generator.

<!-- GENERATED API START -->

## Return conventions

Every public function in header order. *status* means `ZUNE_OK` (0) on success and `ZUNE_BLOWN` (-1) on failure. The last column says whether the call clears `zune_get_error` on entry; calls that cannot fail leave it unchanged.

| Function | Returns | Convention | Clears error |
|---|---|---|---|
| `zune_hd_art_validate` | `ZuneHDArtStatus` | HD result: `ZUNE_HD_ART_OK` or `ZUNE_HD_ART_INVALID` | yes |
| `zune_hd_art_serve` | `ZuneHDArtStatus` | HD result: a `ZuneHDArtStatus` (negative on failure) | yes |
| `zune_hd_art_ensure_artist` | `ZuneHDArtStatus` | HD result: a `ZuneHDArtStatus` (negative on failure) | yes |
| `zune_dossier` | `const char *` | pointer: static version string, never NULL | no |
| `zune_wiretap` | `void` | none | no |
| `zune_set_chatter` | `void` | none | no |
| `zune_breach` | `ZuneDeviceHandle` | pointer-or-NULL: device handle, or NULL | yes |
| `zune_sever` | `void` | none | no |
| `zune_go_dark` | `void` | none | no |
| `zune_get_name` | `const char *` | pointer-or-NULL: borrowed cached string; NULL for a NULL handle | no |
| `zune_get_model` | `const char *` | pointer-or-NULL: borrowed cached string; NULL for a NULL handle | no |
| `zune_get_serial` | `const char *` | pointer-or-NULL: borrowed cached string; NULL for a NULL handle | no |
| `zune_get_battery` | `uint8_t` | value: cached percentage; 0 when unavailable | no |
| `zune_get_capacity` | `uint64_t` | value: cached bytes; 0 for a NULL handle | no |
| `zune_get_headroom` | `uint64_t` | value: cached bytes; 0 for a NULL handle | no |
| `zune_refresh_storage` | `int` | status | yes |
| `zune_identify` | `ZuneModel` | value: a `ZuneModel`; UNKNOWN when unavailable | no |
| `zune_get_family` | `ZuneDeviceFamily` | value: a `ZuneDeviceFamily`; UNKNOWN when unavailable | no |
| `zune_is_hdd` | `int` | boolean: 1 or 0 | no |
| `zune_rename` | `int` | status | yes |
| `zune_is_live` | `int` | boolean: 1 or 0 | no |
| `zune_abort` | `void` | none | no |
| `zune_is_aborted` | `int` | boolean: 1 or 0 | no |
| `zune_clear_abort` | `void` | none | no |
| `zune_autopsy` | `uint16_t` | value: last PTP response code; 0 when none | no |
| `zune_autopsy_name` | `const char *` | pointer: static name, never NULL | no |
| `zune_unjam` | `int` | status | yes |
| `zune_forge_folder` | `uint32_t` | handle-or-0 | yes |
| `zune_get_folders` | `int` | count-or-error: folder count, or `ZUNE_BLOWN` | yes |
| `zune_free_folders` | `void` | none | no |
| `zune_get_tracks` | `ZuneTrack *` | pointer-or-NULL: owned array; NULL when empty or failed | yes |
| `zune_free_tracks` | `void` | none | no |
| `zune_smuggle_track` | `int` | status | yes |
| `zune_smuggle_track_tagged` | `int` | status | yes |
| `zune_purge_track` | `int` | status | yes |
| `zune_extract_track` | `int` | status | yes |
| `zune_get_track_state` | `int` | status | yes |
| `zune_set_track_state` | `int` | status | yes |
| `zune_verify` | `int` | status | yes |
| `zune_rename_item` | `int` | status | yes |
| `zune_sync_notify` | `int` | status | yes |
| `zune_probe_object` | `int` | status | yes |
| `zune_probe_object_named` | `int` | status | yes |
| `zune_get_item_refs` | `int` | status | yes |
| `zune_find_track` | `uint32_t` | handle-or-0: 0 when absent | yes |
| `zune_find_video` | `uint32_t` | handle-or-0: 0 when absent | yes |
| `zune_find_photo` | `uint32_t` | handle-or-0: 0 when absent | yes |
| `zune_get_videos` | `ZuneVideoFile *` | pointer-or-NULL: owned array; NULL when empty or failed | yes |
| `zune_free_videos` | `void` | none | no |
| `zune_smuggle_video_named` | `int` | status: `ZUNE_OK`, `ZUNE_VIDEO_METADATA_INCOMPLETE` (1) or `ZUNE_BLOWN` | yes |
| `zune_smuggle_movie` | `int` | status; deprecated, use `zune_smuggle_video_named` | yes |
| `zune_get_series_info` | `int` | status | yes |
| `zune_smuggle_episode` | `int` | status; deprecated, use `zune_smuggle_video_named` | yes |
| `zune_smuggle_clip` | `int` | status; deprecated, use `zune_smuggle_video_named` | yes |
| `zune_smuggle_other` | `int` | status; deprecated, use `zune_smuggle_video_named` | yes |
| `zune_purge_video` | `int` | status | yes |
| `zune_extract_video` | `int` | status | yes |
| `zune_get_photos` | `ZunePhotoFile *` | pointer-or-NULL: owned array; NULL when empty or failed | yes |
| `zune_free_photos` | `void` | none | no |
| `zune_get_dimensions` | `int` | status | yes |
| `zune_get_photo_albums` | `ZunePhotoAlbum *` | pointer-or-NULL: owned array; NULL when empty or failed | yes |
| `zune_free_photo_albums` | `void` | none | no |
| `zune_arm_photo` | `char *` | pointer-or-NULL: owned temporary path | yes |
| `zune_smuggle_photo` | `int` | status | yes |
| `zune_purge_photo` | `int` | status | yes |
| `zune_extract_photo` | `int` | status | yes |
| `zune_get_playlists` | `ZunePlaylist *` | pointer-or-NULL: owned array; NULL when empty or failed | yes |
| `zune_free_playlists` | `void` | none | no |
| `zune_forge_playlist` | `uint32_t` | handle-or-0 | yes |
| `zune_rewire_playlist` | `int` | status | yes |
| `zune_purge_playlist` | `int` | status | yes |
| `zune_forge_album` | `uint32_t` | handle-or-0 | yes |
| `zune_rewire_album` | `int` | status | yes |
| `zune_forge_artist` | `uint32_t` | handle-or-0 | yes |
| `zune_link_artist` | `int` | status | yes |
| `zune_get_albums` | `ZuneAlbumObject *` | pointer-or-NULL: owned array; NULL when empty or failed | yes |
| `zune_get_albums_checked` | `int` | status | yes |
| `zune_free_albums` | `void` | none | no |
| `zune_get_podcast_series` | `int` | status | yes |
| `zune_free_podcast_series` | `void` | none | no |
| `zune_get_podcast_episodes` | `int` | status | yes |
| `zune_free_podcast_episodes` | `void` | none | no |
| `zune_get_podcast_episode_sources` | `int` | status | yes |
| `zune_free_podcast_episode_sources` | `void` | none | no |
| `zune_get_podcast_states` | `int` | status | yes |
| `zune_free_podcast_states` | `void` | none | no |
| `zune_forge_podcast_series` | `int` | status: `ZUNE_OK`, `ZUNE_PODCAST_FAILED_CLEANED` (-2) or `ZUNE_BLOWN`; ID through `out_id` | yes |
| `zune_smuggle_podcast_episode` | `int` | status: `ZUNE_OK`, `ZUNE_PODCAST_FAILED_CLEANED` (-2) or `ZUNE_BLOWN`; ID through `out_id` | yes |
| `zune_find_podcast_episode` | `int` | status | yes |
| `zune_get_audiobook_parts` | `int` | status | yes |
| `zune_free_audiobook_parts` | `void` | none | no |
| `zune_find_audiobook_part` | `int` | status | yes |
| `zune_smuggle_audiobook_part` | `int` | status | yes |
| `zune_brand` | `int` | status | yes |
| `zune_get_brand_limits` | `int` | status | yes |
| `zune_grab_thumb` | `int` | status | yes |
| `zune_grab_photo_thumb` | `int` | status | yes |
| `zune_arm_audio` | `char *` | pointer-or-NULL: owned temporary path | yes |
| `zune_retag` | `char *` | pointer-or-NULL: owned temporary path | yes |
| `zune_arm_video` | `char *` | pointer-or-NULL: owned temporary path | yes |
| `zune_arm_episode` | `char *` | pointer-or-NULL: owned temporary path | yes |
| `zune_infiltrate_legacy` | `int` | status; deprecated, use `zune_infiltrate` and `zune_free_scan` | yes |
| `zune_free_library` | `void` | none | no |
| `zune_finalize` | `int` | status | yes |
| `zune_decode_filename` | `int` | boolean: 1 when a pattern matched, else 0 | yes |
| `zune_snap_thumb` | `int` | status | yes |
| `zune_probe` | `int` | status | yes |
| `zune_free_metadata` | `void` | none | no |
| `zune_smuggle_track_ex` | `int` | status | yes |
| `zune_infiltrate` | `int` | status | yes |
| `zune_free_scan` | `void` | none | no |
| `zune_dump_raw` | `int` | status | yes |
| `zune_infiltrate_deep` | `int` | status | yes |
| `zune_get_error` | `const char *` | pointer-or-NULL: borrowed thread-local message | no |

## Complete function reference

114 declarations, in public-header order.

### zune_hd_art_validate

```c
ZUNE_API ZuneHDArtStatus zune_hd_art_validate(const ZuneHDArtConfig *config);
```

Software-only full JPEG/UUID/UTF-8 validation. No USB or network. Inputs remain borrowed. Returns OK or INVALID. Requires native libavcodec JPEG decoding support.

[Declaration](../include/zune.h#L121); [implementation index](TOC.md#public-api).

### zune_hd_art_serve

```c
ZUNE_API ZuneHDArtStatus zune_hd_art_serve(struct ZuneDevice *device,
    const ZuneHDArtConfig *config, ZuneHDArtReady ready, void *ready_context,
    ZuneHDArtProgress progress, void *progress_context, ZuneHDArtResult *result);
```

Blocking serialized HD catalog session with full initialization, a single READY callback and safe tunnel closure. Configured buffers remain immutable/borrowed until return. Result carries per-asset and biography acknowledgments, PARTIAL for cache/no-fetch, cancellation and safe_to_continue. Never finalize or retry after unsafe return; sever/reconnect. No hosted server, sockets, deletion, GUID replacement or provider fetching. Physical display and persistence remain separate gates.

[Declaration](../include/zune.h#L132); [implementation index](TOC.md#public-api).

### zune_hd_art_ensure_artist

```c
ZUNE_API ZuneHDArtStatus zune_hd_art_ensure_artist(struct ZuneDevice *device,
    const char *artist_name, const char *catalog_guid, ZuneHDArtistResult *result);
```

Reuse one matching artist with exact catalog GUID, or atomically create a fresh tagged artist after complete artist inventory. Refuses ambiguous names, GUID collisions and existing unbound/different GUIDs without writes. Result retains handle/write_attempted/created on failures; never blindly retry or delete. Requires authenticated HD and serialized ownership. Exact readback is required for success.

[Declaration](../include/zune.h#L143); [implementation index](TOC.md#public-api).

### zune_dossier

```c
ZUNE_API const char *zune_dossier(void);
```

Return the running library version as a static "MAJOR.MINOR.PATCH" string built from the LIBZUNE_VERSION_* macros; never free it. Compare with the header macros to detect a mismatched runtime library.

[Declaration](../include/zune.h#L154); [implementation index](TOC.md#public-api).

### zune_wiretap

```c
ZUNE_API void zune_wiretap(ZuneWiretapFn fn, void *ctx);
```

Install a process-wide log callback receiving level, tag ("libzune", "ptp", "mtp", ...) and message (no tag, no newline; valid only during the call). NULL restores the default stderr sink printing "[tag] message". Called on the logging thread, possibly concurrently; it may log or replace the tap. Set before opening a device. A message already being delivered when the tap changes still reaches the old callback, so keep its ctx valid until every device is closed.

[Declaration](../include/zune.h#L177); [implementation index](TOC.md#public-api).

### zune_set_chatter

```c
ZUNE_API void zune_set_chatter(ZuneChatter level);
```

Set the process-wide minimum level reaching the sink: WHISPER debug detail, TALK lifecycle (default), SHOUT retries/fallbacks, SCREAM failures, SILENT nothing. Out-of-range values are ignored. The initial level comes from ZUNE_CHATTER (whisper|talk|shout|scream|silent, any case) when set.

[Declaration](../include/zune.h#L182); [implementation index](TOC.md#public-api).

### zune_breach

```c
ZUNE_API ZuneDeviceHandle zune_breach(void);
```

Open USB/session and attempt MTPZ authentication if available. Returns a handle or NULL; a non-NULL handle does not prove authentication succeeded. No library scan is performed.

[Declaration](../include/zune.h#L211); [implementation index](TOC.md#public-api).

### zune_sever

```c
ZUNE_API void zune_sever(ZuneDeviceHandle dev);
```

Close session/backend and free the handle. NULL is accepted. Caller must first stop/join every operation using it; the internal delay is not synchronization.

[Declaration](../include/zune.h#L216); [implementation index](TOC.md#public-api).

### zune_go_dark

```c
ZUNE_API void zune_go_dark(void);
```

Process-wide teardown: wipe (volatile overwrite) and free the MTPZ keys that breach loaded once per process, then re-arm loading so a later breach reads them again. Call only after the last sever, with no device open or handshake running; safe when nothing is loaded. Does not touch libgcrypt.

[Declaration](../include/zune.h#L222); [implementation index](TOC.md#public-api).

### zune_get_name

```c
ZUNE_API const char *zune_get_name(ZuneDeviceHandle dev);
```

Borrowed friendly-name string, or NULL for a NULL handle.

[Declaration](../include/zune.h#L226); [implementation index](TOC.md#public-api).

### zune_get_model

```c
ZUNE_API const char *zune_get_model(ZuneDeviceHandle dev);
```

Borrowed model string, or NULL for a NULL handle.

[Declaration](../include/zune.h#L227); [implementation index](TOC.md#public-api).

### zune_get_serial

```c
ZUNE_API const char *zune_get_serial(ZuneDeviceHandle dev);
```

Borrowed serial string, or NULL for a NULL handle.

[Declaration](../include/zune.h#L228); [implementation index](TOC.md#public-api).

### zune_get_battery

```c
ZUNE_API uint8_t     zune_get_battery(ZuneDeviceHandle dev);
```

Connect-time cached battery percentage; zero for a NULL handle or unavailable reading.

[Declaration](../include/zune.h#L229); [implementation index](TOC.md#public-api).

### zune_get_capacity

```c
ZUNE_API uint64_t    zune_get_capacity(ZuneDeviceHandle dev);
```

Cached total storage bytes; refreshed by refresh_storage. Zero for NULL handle.

[Declaration](../include/zune.h#L230); [implementation index](TOC.md#public-api).

### zune_get_headroom

```c
ZUNE_API uint64_t    zune_get_headroom(ZuneDeviceHandle dev);
```

Cached free storage bytes; refreshed by refresh_storage. Zero for NULL handle.

[Declaration](../include/zune.h#L231); [implementation index](TOC.md#public-api).

### zune_refresh_storage

```c
ZUNE_API int         zune_refresh_storage(ZuneDeviceHandle dev);
```

Read live storage info, update total/free cache on success. 0 success, -1 failure; failure preserves old cached values.

[Declaration](../include/zune.h#L237); [implementation index](TOC.md#public-api).

### zune_identify

```c
ZUNE_API ZuneModel   zune_identify(ZuneDeviceHandle dev);
```

Return model/profile family inferred from device family/model data, or UNKNOWN. HD 720p is a caller-selected profile rather than a separate device family.

[Declaration](../include/zune.h#L238); [implementation index](TOC.md#public-api).

### zune_get_family

```c
ZUNE_API ZuneDeviceFamily zune_get_family(ZuneDeviceHandle dev);
```

Return cached device family or UNKNOWN when unavailable.

[Declaration](../include/zune.h#L243); [implementation index](TOC.md#public-api).

### zune_is_hdd

```c
ZUNE_API int zune_is_hdd(ZuneDeviceHandle dev);
```

Return 1 for Keel/Draco, otherwise 0; unknown is not evidence of flash storage.

[Declaration](../include/zune.h#L247); [implementation index](TOC.md#public-api).

### zune_rename

```c
ZUNE_API int zune_rename(ZuneDeviceHandle dev, const char *new_name);
```

Set the device friendly name. 0 success, -1 failure.

[Declaration](../include/zune.h#L250); [implementation index](TOC.md#public-api).

### zune_is_live

```c
ZUNE_API int zune_is_live(ZuneDeviceHandle dev);
```

Return 1 if local handle/backend state looks connected, else 0. Does not probe USB responsiveness.

[Declaration](../include/zune.h#L254); [implementation index](TOC.md#public-api).

### zune_abort

```c
ZUNE_API void zune_abort(ZuneDeviceHandle dev);
```

Request cooperative cancellation. Wait for the active operation to return before teardown; cancellation does not promise rollback of device objects.

[Declaration](../include/zune.h#L258); [implementation index](TOC.md#public-api).

### zune_is_aborted

```c
ZUNE_API int zune_is_aborted(ZuneDeviceHandle dev);
```

Return the cancellation flag, or 0 for NULL.

[Declaration](../include/zune.h#L261); [implementation index](TOC.md#public-api).

### zune_clear_abort

```c
ZUNE_API void zune_clear_abort(ZuneDeviceHandle dev);
```

Clear cancellation before a new operation, after the old operation has finished.

[Declaration](../include/zune.h#L264); [implementation index](TOC.md#public-api).

### zune_autopsy

```c
ZUNE_API uint16_t zune_autopsy(ZuneDeviceHandle dev);
```

Return the last PTP response code, including synthetic transport failure 0x02FF; 0 for NULL/no response. After a failed track, photo or video upload it names the failed send, not the orphan cleanup that followed. Subsequent operations can replace it.

[Declaration](../include/zune.h#L271); [implementation index](TOC.md#public-api).

### zune_autopsy_name

```c
ZUNE_API const char *zune_autopsy_name(uint16_t code);
```

Return a borrowed static name for a PTP response code, including a fallback for unknown codes.

[Declaration](../include/zune.h#L274); [implementation index](TOC.md#public-api).

### zune_unjam

```c
ZUNE_API int zune_unjam(ZuneDeviceHandle dev);
```

Ask the backend to clear both USB endpoints. 0 success, -1 failure; not a guarantee that the device transaction was rolled back or can be retried.

[Declaration](../include/zune.h#L279); [implementation index](TOC.md#public-api).

### zune_forge_folder

```c
ZUNE_API uint32_t zune_forge_folder(ZuneDeviceHandle dev, const char *name,
                             uint32_t parent_id);
```

Create a folder in primary storage under parent_id. Return nonzero ID or 0 on failure.

[Declaration](../include/zune.h#L283); [implementation index](TOC.md#public-api).

### zune_get_folders

```c
ZUNE_API int zune_get_folders(ZuneDeviceHandle dev, ZuneFolderEntry **out);
```

Enumerate folder ID/name entries. Return count (0 for a device with no folders) or -1 with get_error set when the folder query fails or the device stops answering partway through; a folder whose details cannot be read is left out. Release the allocated output using free_folders.

[Declaration](../include/zune.h#L291); [implementation index](TOC.md#public-api).

### zune_free_folders

```c
ZUNE_API void zune_free_folders(ZuneFolderEntry *entries, int count);
```

Release folder entry names and their array with the returned count.

[Declaration](../include/zune.h#L292); [implementation index](TOC.md#public-api).

### zune_get_tracks

```c
ZUNE_API ZuneTrack *zune_get_tracks(ZuneDeviceHandle dev, int *count);
```

Enumerate tracks through MTP into an owned array and count. NULL with count 0 is an empty library, or a failed handle query when get_error was set by this call; unreadable objects can be skipped.

[Declaration](../include/zune.h#L313); [implementation index](TOC.md#public-api).

### zune_free_tracks

```c
ZUNE_API void zune_free_tracks(ZuneTrack *tracks, int count);
```

Release a separately returned track array and strings. Do not use on a subarray owned by a scan you will also free.

[Declaration](../include/zune.h#L314); [implementation index](TOC.md#public-api).

### zune_smuggle_track

```c
ZUNE_API int zune_smuggle_track(ZuneDeviceHandle dev, const char *filepath,
                    uint32_t *out_item_id);
```

Upload audio bytes as-is. File tags are not read: the title is the filename without extension and the artist "Unknown Artist"; use smuggle_track_tagged for metadata. Does not transcode or create the album/artist hierarchy. 0 success, -1 failure; optional output receives ID on success.

[Declaration](../include/zune.h#L320); [implementation index](TOC.md#public-api).

### zune_smuggle_track_tagged

```c
ZUNE_API int zune_smuggle_track_tagged(ZuneDeviceHandle dev, const char *filepath,
                               const char *title, const char *artist,
                               const char *album, const char *genre,
                               uint16_t tracknumber, uint32_t duration_ms,
                               uint32_t *out_item_id);
```

Upload audio using explicit title/artist/album/genre/track/duration values. Does not transcode. 0 success, -1 failure. Streams the file; a failed payload removes the partial object and retries up to three attempts unless the device refused on purpose; an object that cannot be removed is named in get_error and ends the retries.

[Declaration](../include/zune.h#L324); [implementation index](TOC.md#public-api).

### zune_purge_track

```c
ZUNE_API int zune_purge_track(ZuneDeviceHandle dev, uint32_t item_id);
```

Delete one device object. 0 success, -1 failure; caller must ensure the ID is the intended track.

[Declaration](../include/zune.h#L331); [implementation index](TOC.md#public-api).

### zune_extract_track

```c
ZUNE_API int zune_extract_track(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);
```

Download an object to dest_path. 0 success, -1 failure; inspect/remove partial output after failure as appropriate.

[Declaration](../include/zune.h#L334); [implementation index](TOC.md#public-api).

### zune_get_track_state

```c
ZUNE_API int zune_get_track_state(ZuneDeviceHandle dev, uint32_t item_id,
                          uint16_t *out_playcount, uint8_t *out_rating,
                          uint32_t *out_skip_count);
```

Read requested playcount/rating. 0 if at least one was read, -1 otherwise; failed fields become zero and skip count always becomes zero. Output pointers may be NULL.

[Declaration](../include/zune.h#L342); [implementation index](TOC.md#public-api).

### zune_set_track_state

```c
ZUNE_API int zune_set_track_state(ZuneDeviceHandle dev, uint32_t item_id,
                          uint16_t playcount, uint8_t rating);
```

Write playcount and rating (0 neutral, 8 liked, 3 disliked). 0 if both writes succeed, -1 if either fails; partial updates are possible.

[Declaration](../include/zune.h#L349); [implementation index](TOC.md#public-api).

### zune_verify

```c
ZUNE_API int zune_verify(ZuneDeviceHandle dev, uint32_t item_id,
                 const char *expected_title, const char *expected_artist,
                 uint64_t expected_size);
```

Read back size/title/artist and compare. NULL text skips that check; size 0 skips size. 0 if enabled checks pass, -1 otherwise. Does not compare file content or playback.

[Declaration](../include/zune.h#L357); [implementation index](TOC.md#public-api).

### zune_rename_item

```c
ZUNE_API int zune_rename_item(ZuneDeviceHandle dev, uint32_t item_id, const char *new_name);
```

Set object Name (display title), preserving ObjectFileName. 0 success, -1 failure. Does not retag the media file.

[Declaration](../include/zune.h#L364); [implementation index](TOC.md#public-api).

### zune_sync_notify

```c
ZUNE_API int zune_sync_notify(ZuneDeviceHandle dev, const char *name, uint32_t op_kind,
                     uint32_t item_index, uint32_t total_items,
                     uint32_t progress_before, uint32_t progress_after);
```

Best-effort on-device batch notification. op_kind 0 upload, 1 download; item_index is 1-based, progress fields are percentages 0..100. 0 success, -1 failure.

[Declaration](../include/zune.h#L377); [implementation index](TOC.md#public-api).

### zune_probe_object

```c
ZUNE_API int zune_probe_object(ZuneDeviceHandle dev, uint32_t item_id,
                      uint64_t *out_size, uint16_t *out_format);
```

GetObjectInfo existence/size/format probe. Optional size/format outputs. 0 success, -1 failure; transport failure is not proof the object does not exist.

[Declaration](../include/zune.h#L385); [implementation index](TOC.md#public-api).

### zune_probe_object_named

```c
ZUNE_API int zune_probe_object_named(ZuneDeviceHandle dev, uint32_t item_id,
                            uint64_t *out_size, uint16_t *out_format,
                            char **out_name);
```

Object probe plus allocated ObjectFileName, when requested/available. Caller frees name. 0 success, -1 failure.

[Declaration](../include/zune.h#L389); [implementation index](TOC.md#public-api).

### zune_get_item_refs

```c
ZUNE_API int zune_get_item_refs(ZuneDeviceHandle dev, uint32_t item_id,
                       uint32_t **out_refs, int *out_count);
```

Read object reference IDs into an allocated array and count. Caller frees the array. 0 success, -1 failure.

[Declaration](../include/zune.h#L395); [implementation index](TOC.md#public-api).

### zune_find_track

```c
ZUNE_API uint32_t zune_find_track(ZuneDeviceHandle dev,
                          const char *title, const char *artist,
                          const char *album);
```

Search the live borrowed scan snapshot by case-insensitive title+artist+album. Return first ID or 0. No disc/track/content matching; keep scan allocated.

[Declaration](../include/zune.h#L405); [implementation index](TOC.md#public-api).

### zune_find_video

```c
ZUNE_API uint32_t zune_find_video(ZuneDeviceHandle dev, const char *filename);
```

Search the borrowed scan's legacy filename field case-insensitively; that field can be a ZMDB display title. Return first ID or 0. Keep scan allocated.

[Declaration](../include/zune.h#L412); [implementation index](TOC.md#public-api).

### zune_find_photo

```c
ZUNE_API uint32_t zune_find_photo(ZuneDeviceHandle dev, const char *filename);
```

Search the borrowed scan's photo filename case-insensitively. Return first ID or 0. Keep scan allocated; no folder/content disambiguation.

[Declaration](../include/zune.h#L416); [implementation index](TOC.md#public-api).

### zune_get_videos

```c
ZUNE_API ZuneVideoFile *zune_get_videos(ZuneDeviceHandle dev, int *count);
```

Enumerate videos via MTP into an owned array and count. Explicit title/ObjectFileName fields distinguish display from transport names. NULL can mean empty/error.

[Declaration](../include/zune.h#L441); [implementation index](TOC.md#public-api).

### zune_free_videos

```c
ZUNE_API void zune_free_videos(ZuneVideoFile *videos, int count);
```

Release a separately allocated video array, including filename, title and object_filename strings.

[Declaration](../include/zune.h#L442); [implementation index](TOC.md#public-api).

### zune_smuggle_video_named

```c
ZUNE_API int zune_smuggle_video_named(ZuneDeviceHandle dev, const char *filepath,
                            const char *object_filename, const char *title,
                            uint16_t meta_genre,
                            const char *series, int season, int episode,
                            const char *description,
                            const uint8_t *poster_jpeg, size_t poster_len,
                            uint32_t *out_item_id);
```

Preferred video upload. Independent transport filename and title; requires non-NULL out_item_id. 0 complete, 1 media uploaded but metadata/art incomplete, -1 validation/file-transfer failure. For 0/1 returned ID is valid. TV series properties apply only for TV genre and nonempty series; season/episode must be nonnegative. Never reupload for result 1.

[Declaration](../include/zune.h#L460); [implementation index](TOC.md#public-api).

### zune_smuggle_movie

```c
ZUNE_API int zune_smuggle_movie(ZuneDeviceHandle dev, const char *filepath,
                     const char *display_name, const char *description,
                     const uint8_t *poster_jpeg, size_t poster_len,
                     uint32_t *out_item_id) ZUNE_DEPRECATED("use zune_smuggle_video_named");
```

Legacy movie upload; display_name is used for transport/display. 0 means media upload succeeded, even if later metadata/poster failed. Prefer smuggle_video_named.

[Declaration](../include/zune.h#L475); [implementation index](TOC.md#public-api).

### zune_get_series_info

```c
ZUNE_API int zune_get_series_info(ZuneDeviceHandle dev, uint32_t item_id,
                                char **out_series, int *out_season, int *out_episode,
                                char **out_title);
```

Read series/season/episode and optional title (Name, then filename fallback). 0 when nonempty series found, -1 otherwise. Caller frees returned series/title strings, including outputs returned on a failed series lookup.

[Declaration](../include/zune.h#L483); [implementation index](TOC.md#public-api).

### zune_smuggle_episode

```c
ZUNE_API int zune_smuggle_episode(ZuneDeviceHandle dev, const char *filepath,
                          const char *display_name,
                          const char *series, int season, int episode,
                          const char *description,
                          const uint8_t *poster_jpeg, size_t poster_len,
                          uint32_t *out_item_id) ZUNE_DEPRECATED("use zune_smuggle_video_named");
```

Legacy TV upload; synthesizes Series - SxxEyy title and only sets series data for positive episode. 0 does not certify metadata/art. Prefer smuggle_video_named.

[Declaration](../include/zune.h#L490); [implementation index](TOC.md#public-api).

### zune_smuggle_clip

```c
ZUNE_API int zune_smuggle_clip(ZuneDeviceHandle dev, const char *filepath,
                           const char *display_name,
                           const uint8_t *poster_jpeg, size_t poster_len,
                           uint32_t *out_item_id) ZUNE_DEPRECATED("use zune_smuggle_video_named");
```

Legacy music-video upload; display_name used for transport/display. 0 does not certify metadata/art. Prefer smuggle_video_named.

[Declaration](../include/zune.h#L498); [implementation index](TOC.md#public-api).

### zune_smuggle_other

```c
ZUNE_API int zune_smuggle_other(ZuneDeviceHandle dev, const char *filepath,
                       const char *display_name, const char *description,
                       const uint8_t *poster_jpeg, size_t poster_len,
                       uint32_t *out_item_id) ZUNE_DEPRECATED("use zune_smuggle_video_named");
```

Legacy Other video upload; display_name used for transport/display. 0 does not certify metadata/art. Prefer smuggle_video_named.

[Declaration](../include/zune.h#L504); [implementation index](TOC.md#public-api).

### zune_purge_video

```c
ZUNE_API int zune_purge_video(ZuneDeviceHandle dev, uint32_t item_id);
```

Delete a video object. 0 success, -1 failure with get_error naming the device response.

[Declaration](../include/zune.h#L510); [implementation index](TOC.md#public-api).

### zune_extract_video

```c
ZUNE_API int zune_extract_video(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);
```

Download a video object to dest_path. 0 success, -1 failure with get_error set. No direct playback API is provided.

[Declaration](../include/zune.h#L513); [implementation index](TOC.md#public-api).

### zune_get_photos

```c
ZUNE_API ZunePhotoFile *zune_get_photos(ZuneDeviceHandle dev, int *count);
```

Enumerate photos via MTP into an allocated array and count. NULL can mean empty/error.

[Declaration](../include/zune.h#L532); [implementation index](TOC.md#public-api).

### zune_free_photos

```c
ZUNE_API void zune_free_photos(ZunePhotoFile *photos, int count);
```

Release a separately returned photo array and strings.

[Declaration](../include/zune.h#L533); [implementation index](TOC.md#public-api).

### zune_get_dimensions

```c
ZUNE_API int zune_get_dimensions(ZuneDeviceHandle dev, uint32_t item_id,
                               uint32_t *width, uint32_t *height);
```

Read width/height MTP properties. Supply both output pointers. 0 success, -1 failure.

[Declaration](../include/zune.h#L537); [implementation index](TOC.md#public-api).

### zune_get_photo_albums

```c
ZUNE_API ZunePhotoAlbum *zune_get_photo_albums(ZuneDeviceHandle dev,
                                       ZunePhotoFile *photos, int photo_count,
                                       int *album_count);
```

Group supplied photos by parent folder and resolve names from this device's folder cache (names unknown to it read "Photos"). A NULL album_count returns NULL. Returned folder albums are allocated; input photos remain caller-owned. Use free_photo_albums.

[Declaration](../include/zune.h#L541); [implementation index](TOC.md#public-api).

### zune_free_photo_albums

```c
ZUNE_API void zune_free_photo_albums(ZunePhotoAlbum *albums, int count);
```

Release allocated photo-album names and array.

[Declaration](../include/zune.h#L544); [implementation index](TOC.md#public-api).

### zune_arm_photo

```c
ZUNE_API char *zune_arm_photo(const char *input_path);
```

Prepare a max-480px JPEG via sips (macOS, ZUNE_SIPS) or ffmpeg (Linux, ZUNE_FFMPEG/PATH). Return a unique owned 0600 temporary path under $TMPDIR, or NULL with no file left behind. Unlink the file and free the path.

[Declaration](../include/zune.h#L549); [implementation index](TOC.md#public-api).

### zune_smuggle_photo

```c
ZUNE_API int zune_smuggle_photo(ZuneDeviceHandle dev, const char *filepath,
                     const char *album_name);
```

Prepare JPEG automatically and send to a named album folder; NULL album sends to root. If conversion fails, only an original that is already JPEG is sent; anything else fails before device traffic. An album missing from this device's folder cache (per device handle, freed by sever) is looked up on the device before one is created, and an unreadable folder list fails rather than risk a duplicate. 0 success, -1 failure. No uploaded-ID output is exposed.

[Declaration](../include/zune.h#L554); [implementation index](TOC.md#public-api).

### zune_purge_photo

```c
ZUNE_API int zune_purge_photo(ZuneDeviceHandle dev, uint32_t item_id);
```

Delete a photo object. 0 success, -1 failure with get_error naming the device response.

[Declaration](../include/zune.h#L558); [implementation index](TOC.md#public-api).

### zune_extract_photo

```c
ZUNE_API int zune_extract_photo(ZuneDeviceHandle dev, uint32_t item_id,
                         const char *dest_path);
```

Download photo to dest_path. 0 success, -1 failure with get_error set.

[Declaration](../include/zune.h#L561); [implementation index](TOC.md#public-api).

### zune_get_playlists

```c
ZUNE_API ZunePlaylist *zune_get_playlists(ZuneDeviceHandle dev, int *count);
```

Enumerate playlists and references through MTP. Owned array/count; NULL can mean empty/error. Unreadable entries or references can yield partial results.

[Declaration](../include/zune.h#L574); [implementation index](TOC.md#public-api).

### zune_free_playlists

```c
ZUNE_API void zune_free_playlists(ZunePlaylist *playlists, int count);
```

Release a separately returned playlist array, names and track-ID arrays.

[Declaration](../include/zune.h#L575); [implementation index](TOC.md#public-api).

### zune_forge_playlist

```c
ZUNE_API uint32_t zune_forge_playlist(ZuneDeviceHandle dev, const char *name,
                               uint32_t *track_ids, int count);
```

Create an abstract playlist with supplied track IDs/order. Return nonzero object ID or 0. If the commit or the track references fail, the new playlist is deleted, 0 is returned, get_error says whether that cleanup worked and autopsy keeps the original failure. Does not upload tracks.

[Declaration](../include/zune.h#L578); [implementation index](TOC.md#public-api).

### zune_rewire_playlist

```c
ZUNE_API int zune_rewire_playlist(ZuneDeviceHandle dev, uint32_t playlist_id,
                          const char *name,
                          uint32_t *track_ids, int count);
```

Update playlist name and supplied membership/order. 0 success, -1 failure. This is not an automatic append or transaction across both fields.

[Declaration](../include/zune.h#L582); [implementation index](TOC.md#public-api).

### zune_purge_playlist

```c
ZUNE_API int zune_purge_playlist(ZuneDeviceHandle dev, uint32_t playlist_id);
```

Delete the playlist object, not its referenced media. 0 success, -1 failure.

[Declaration](../include/zune.h#L587); [implementation index](TOC.md#public-api).

### zune_forge_album

```c
ZUNE_API uint32_t zune_forge_album(ZuneDeviceHandle dev,
                                   const char *album_name,
                                   const char *artist_name,
                                   const char *genre,
                                   uint32_t *track_ids, int track_count);
```

Create an abstract music album and set supplied references. Return nonzero ID or 0. If the commit or the track references fail, the new album is deleted, 0 is returned, get_error says whether that cleanup worked and autopsy keeps the original failure. Create/reuse and link artist separately.

[Declaration](../include/zune.h#L598); [implementation index](TOC.md#public-api).

### zune_rewire_album

```c
ZUNE_API int zune_rewire_album(ZuneDeviceHandle dev, uint32_t album_id,
                              const char *album_name,
                              const char *artist_name,
                              uint32_t *track_ids, int track_count);
```

Update album names and replace references with the supplied complete list when nonempty. Caller performs any merge. Empty input does not clear references. Name/artist failures only log warnings, so 0 does not certify metadata; -1 reports invalid device/reference failure.

[Declaration](../include/zune.h#L607); [implementation index](TOC.md#public-api).

### zune_forge_artist

```c
ZUNE_API uint32_t zune_forge_artist(ZuneDeviceHandle dev,
                                    const char *artist_name);
```

Reuse a live matching artist, otherwise create a zero-byte abstract artist. Return ID or 0; failed inventory blocks creation. ASCII case/outer-space matching preserves UTF-8 bytes; no duplicate cleanup.

[Declaration](../include/zune.h#L617); [implementation index](TOC.md#public-api).

### zune_link_artist

```c
ZUNE_API int zune_link_artist(ZuneDeviceHandle dev, uint32_t object_id,
                        uint32_t artist_handle);
```

Write ArtistId for an album/track. 0 success, -1 failure.

[Declaration](../include/zune.h#L621); [implementation index](TOC.md#public-api).

### zune_get_albums

```c
ZUNE_API ZuneAlbumObject *zune_get_albums(ZuneDeviceHandle dev, int *count);
```

Enumerate abstract albums, names, artists and references via MTP. Owned array/count; NULL can mean empty/error; property failures can produce incomplete records.

[Declaration](../include/zune.h#L633); [implementation index](TOC.md#public-api).

### zune_get_albums_checked

```c
ZUNE_API int zune_get_albums_checked(struct ZuneDevice *device,
                           ZuneAlbumObject **albums, int *count);
```

Read a strict complete album inventory, including names, artist names and references. Return 0 including proven empty, -1 with NULL/zero outputs on any unreadable or malformed object. Caller frees successful arrays with free_albums. No USB writes.

[Declaration](../include/zune.h#L638); [implementation index](TOC.md#public-api).

### zune_free_albums

```c
ZUNE_API void zune_free_albums(ZuneAlbumObject *albums, int count);
```

Release album array, strings and track-ID arrays returned by get_albums.

[Declaration](../include/zune.h#L641); [implementation index](TOC.md#public-api).

### zune_get_podcast_series

```c
ZUNE_API int zune_get_podcast_series(ZuneDeviceHandle dev, ZunePodcastSeries **series, int *count);
```

Experimental complete MTP series inventory with exact feed identity. Requires successful MTPZ authentication; unauthenticated sessions fail rather than reporting an empty library. 0 success, including NULL array/count 0 when empty; -1 failure with no partial result. Use free_podcast_series. Synchronous, requiring the same serializer as all other device calls. Keel roundtrip tested; other-model acceptance pending.

[Declaration](../include/zune.h#L662); [implementation index](TOC.md#public-api).

### zune_free_podcast_series

```c
ZUNE_API void zune_free_podcast_series(ZunePodcastSeries *series, int count);
```

Free the owned series array and each name/author/feed string using its returned count. NULL is accepted.

[Declaration](../include/zune.h#L663); [implementation index](TOC.md#public-api).

### zune_get_podcast_episodes

```c
ZUNE_API int zune_get_podcast_episodes(ZuneDeviceHandle dev, ZunePodcastEpisode **episodes, int *count);
```

Read-only native schema-0x10 episode inventory from complete wildcard handles. Authenticated active Keel/Pavo/Draco sessions only. Both output pointers required; 0 success including NULL/count zero when empty, -1 failure with no partial output. Owned array and title/author/series_name strings use free_podcast_episodes. Group by series_id, never title; firmware may return empty series_name, resolved through series inventory. Exact property widths and Unicode validated, transport failures stop immediately. No writes or playback guarantees.

[Declaration](../include/zune.h#L678); [implementation index](TOC.md#public-api).

### zune_free_podcast_episodes

```c
ZUNE_API void zune_free_podcast_episodes(ZunePodcastEpisode *episodes, int count);
```

Free a get_podcast_episodes array and all its strings with the returned count. NULL accepted.

[Declaration](../include/zune.h#L679); [implementation index](TOC.md#public-api).

### zune_get_podcast_episode_sources

```c
ZUNE_API int zune_get_podcast_episode_sources(ZuneDeviceHandle dev, ZunePodcastEpisodeSource **sources, int *count);
```

Read-only complete exact feed/source episode mapping for authenticated active Keel/Pavo/Draco. Reads series once, then each distinct series folder's MP3 inventory once; validates exact series relationship, live parent/format and podcast classification. Both outputs required. 0 succeeds including NULL/count 0 when known empty; -1 unavailable, malformed or ambiguous with no partial output. Duplicate feeds, exact feed/source pairs or handles fail closed. Owned array plus UTF-8 feed_url/source_url strings use free_podcast_episode_sources. Same source URL under distinct feeds remains distinct. No title heuristics, mutations or deletion authorization. Serialize all device I/O.

[Declaration](../include/zune.h#L693); [implementation index](TOC.md#public-api).

### zune_free_podcast_episode_sources

```c
ZUNE_API void zune_free_podcast_episode_sources(ZunePodcastEpisodeSource *sources, int count);
```

Free owned feed_url/source_url strings and the get_podcast_episode_sources array using the returned count. NULL accepted.

[Declaration](../include/zune.h#L694); [implementation index](TOC.md#public-api).

### zune_get_podcast_states

```c
ZUNE_API int zune_get_podcast_states(ZuneDeviceHandle dev, ZunePodcastState **states, int *count);
```

Read fresh persisted podcast state from a complete bounded ZMDB snapshot. Requires authenticated active Keel/Draco with v2 or Pavo with v5; serialize device I/O. Both output pointers required. 0 means complete success, including NULL/count 0 when known empty; -1 means unavailable, malformed, canceled or incomplete and returns NULL/count 0. Caller owns array, released by free_podcast_states. Exposes exact atom/series IDs, owned validated UTF-8 title, duration, bookmark, raw flags and marked_played mask 0x200. Bookmark and played are independent. This records firmware state, not proof of uninterrupted natural completion, and never authorizes deletion. Join fresh live identities including exact title and duration before a separate policy acts; unknown state must preserve media.

[Declaration](../include/zune.h#L714); [implementation index](TOC.md#public-api).

### zune_free_podcast_states

```c
ZUNE_API void zune_free_podcast_states(ZunePodcastState *states, int count);
```

Release the owned get_podcast_states array and its title strings using the returned count. NULL accepted.

[Declaration](../include/zune.h#L715); [implementation index](TOC.md#public-api).

### zune_forge_podcast_series

```c
ZUNE_API int zune_forge_podcast_series(ZuneDeviceHandle dev, uint32_t parent_id,
                             const char *filename, const char *name,
                             const char *author, const char *feed_url,
                             uint32_t *out_id);
```

Experimental zero-byte podcast series creation under an explicit parent, or reuse of exactly one live exact feed-URL match. Requires successful MTPZ authentication. Never merges by title or overwrites existing metadata. Descriptor and property readback checks. Required out_id is zeroed first; 0 return succeeds, negative fails. ZUNE_PODCAST_FAILED_CLEANED (-2) confirms deletion of the new object; other failures return -1. A nonzero ID on failure identifies only a new orphan whose cleanup failed: inspect before retrying. No implicit artwork or finalize. Keel roundtrip tested; other-model acceptance pending.

[Declaration](../include/zune.h#L726); [implementation index](TOC.md#public-api).

### zune_smuggle_podcast_episode

```c
ZUNE_API int zune_smuggle_podcast_episode(ZuneDeviceHandle dev, uint32_t parent_id,
                                 const char *filepath,
                                 const ZunePodcastEpisodeInput *episode,
                                 uint32_t *out_id);
```

Experimental caller-prepared MP3 episode upload, with podcast category and series relationship. Requires successful MTPZ authentication; no transcoding or codec sniffing. Live series/source-URL identity within the supplied parent folder plus title/size checks controls reuse; ambiguous matches fail without writes. Caller retains feed/GUID identity across URL changes. Firmware may normalize filenames. Keel/Pavo/Draco schema 0x10 supports zero MetaGenre/empty redundant SeriesName readback; series link/URL/title and format/size must match. 0 success, negative failure (including ZUNE_PODCAST_FAILED_CLEANED), required out_id uses the same orphan contract as forge_podcast_series. Uploads through the shared engine: a failed payload removes the new episode and retries up to three attempts; an unanswered creation is never retried; FAILED_CLEANED means every episode this call created was removed. No implicit finalize or played/bookmark guarantee. Keel upload/reuse/extraction/cleanup and new-session repeat tested; user confirmed Classic playback and show artwork. Resume and other models remain unverified.

[Declaration](../include/zune.h#L756); [implementation index](TOC.md#public-api).

### zune_find_podcast_episode

```c
ZUNE_API int zune_find_podcast_episode(ZuneDeviceHandle dev, const char *feed_url,
                              const char *source_url, uint32_t *parent_id,
                              uint32_t *series_id, uint32_t *episode_id);
```

Read-only queue admission lookup by exact feed URL and episode source URL. Requires MTPZ authentication. Returns 0 for a complete lookup (zero IDs mean absent), -1 for failure with zeroed outputs. Returns the live series parent, series ID and episode ID; ambiguous series/episodes, unreadable inventory/links and wrong classification fail closed. Does not compare title or byte size, download audio, create or rewrite objects. Caller persists feed/GUID-to-source identity and handles changed downloads explicitly.

[Declaration](../include/zune.h#L765); [implementation index](TOC.md#public-api).

### zune_get_audiobook_parts

```c
ZUNE_API int zune_get_audiobook_parts(ZuneDeviceHandle dev, ZuneAudiobookPart **parts, int *count);
```

Read-only native schema-0x12 book-part inventory from complete wildcard handles, including HD objects omitted by format-filtered queries. Same authentication, family, required-output and all-or-nothing contract as get_podcast_episodes. Group by parent_id, never book_title; sort by part_number in the caller. Owned array and title/author/book_title strings use free_audiobook_parts. No format conversion or device writes.

[Declaration](../include/zune.h#L780); [implementation index](TOC.md#public-api).

### zune_free_audiobook_parts

```c
ZUNE_API void zune_free_audiobook_parts(ZuneAudiobookPart *parts, int count);
```

Free a get_audiobook_parts array and all its strings with the returned count. NULL accepted.

[Declaration](../include/zune.h#L781); [implementation index](TOC.md#public-api).

### zune_find_audiobook_part

```c
ZUNE_API int zune_find_audiobook_part(ZuneDeviceHandle dev, const ZuneAudiobookPartInput *part,
                            uint32_t *folder_id, uint32_t *part_id);
```

Experimental read-only live lookup by caller-provided book content key and positive part number. Requires authentication and tested Keel/Pavo/Draco family. Validates book/title/author/duration/category and refuses conflicting same-title books in other folders. 0 complete, -1 failure; required folder/part outputs zero on failure, zero part means absent. No ZMDB cache or writes.

[Declaration](../include/zune.h#L794); [implementation index](TOC.md#public-api).

### zune_smuggle_audiobook_part

```c
ZUNE_API int zune_smuggle_audiobook_part(ZuneDeviceHandle dev, const char *filepath,
                               const ZuneAudiobookPartInput *part, uint32_t *out_id);
```

Experimental prepared immutable MP3 upload under an identity-named folder inside OverDrive. No music artist/album/reference creation. Reuses live book/part identity plus metadata and size; descriptors checked before creation. 0 success, -1 failure. Uploads through the shared engine: a failed payload removes the new part and retries up to three attempts; an unanswered creation is never retried. Required output retains a part ID on failure only when that part could not be removed or failed readback after upload; lost responses can leave uncertain state even with ID zero. No codec sniffing, conversion, chapter splitting, artwork or finalization. Serialized authenticated Keel/Pavo/Draco only; long files and resume remain separate gates.

[Declaration](../include/zune.h#L803); [implementation index](TOC.md#public-api).

### zune_brand

```c
ZUNE_API int zune_brand(ZuneDeviceHandle dev, uint32_t item_id,
                        const uint8_t *jpeg_data, size_t jpeg_len);
```

Write JPEG representative-sample artwork to an object. 0 success, -1 failure; image is not automatically resized by this API.

[Declaration](../include/zune.h#L810); [implementation index](TOC.md#public-api).

### zune_get_brand_limits

```c
ZUNE_API int zune_get_brand_limits(ZuneDeviceHandle dev, uint32_t item_id,
                                   uint32_t *max_width, uint32_t *max_height,
                                   uint32_t *max_bytes);
```

Read the maximum artwork width/height (pixels) and JPEG size (bytes) the device declares for the item's object format; 0 means not declared. Reads ObjectInfo, then the format's descriptors once per connection (cached until sever). Unsupported properties are 0; a transport failure or malformed descriptor returns -1 with all outputs 0 and get_error set. Firmware can store larger images than it declares yet not display them; resize before brand.

[Declaration](../include/zune.h#L819); [implementation index](TOC.md#public-api).

### zune_grab_thumb

```c
ZUNE_API int zune_grab_thumb(ZuneDeviceHandle dev, uint32_t item_id,
                        const char *cache_path);
```

Try representative sample then MTP thumbnail, writing cache_path. 0 success, -1 failure. Does not download the complete media file.

[Declaration](../include/zune.h#L826); [implementation index](TOC.md#public-api).

### zune_grab_photo_thumb

```c
ZUNE_API int zune_grab_photo_thumb(ZuneDeviceHandle dev, uint32_t item_id,
                              const char *cache_path);
```

Photo-only fallback: representative sample, MTP thumbnail, then complete photo download/resize. 0 success, -1 failure. Do not use as a video thumbnail API.

[Declaration](../include/zune.h#L832); [implementation index](TOC.md#public-api).

### zune_arm_audio

```c
ZUNE_API char *zune_arm_audio(const char *input_path);
```

Legacy MP3 320kbps/ID3v2.3 preparation, with source metadata stripped for separate MTP tagging. Runs ffmpeg found via ZUNE_FFMPEG/PATH. Owned temporary path or NULL; unlink and free.

[Declaration](../include/zune.h#L841); [implementation index](TOC.md#public-api).

### zune_retag

```c
ZUNE_API char *zune_retag(const char *mp3_path);
```

Legacy MP3 stream-copy retag to ID3v2.3. Runs ffmpeg found via ZUNE_FFMPEG/PATH. Owned temporary path or NULL; unlink and free.

[Declaration](../include/zune.h#L846); [implementation index](TOC.md#public-api).

### zune_arm_video

```c
ZUNE_API char *zune_arm_video(const char *input_path, ZuneModel model);
```

Legacy transcode by ZuneModel profile (WMV for 30, H.264 for other supported profiles). Runs ffmpeg found via ZUNE_FFMPEG/PATH. Owned temporary path or NULL; unlink and free.

[Declaration](../include/zune.h#L851); [implementation index](TOC.md#public-api).

### zune_arm_episode

```c
ZUNE_API char *zune_arm_episode(const char *input_path, ZuneModel model,
                                 const char *series, int season, int episode);
```

Legacy video preparation with series/season/episode container metadata. Same tool lookup and temp ownership as arm_video.

[Declaration](../include/zune.h#L855); [implementation index](TOC.md#public-api).

### zune_infiltrate_legacy

```c
ZUNE_API int zune_infiltrate_legacy(ZuneDeviceHandle dev, ZuneLibrary **out)
    ZUNE_DEPRECATED("use zune_infiltrate and zune_free_scan");
```

Deprecated tracks/videos/photos-only ZMDB result. 0 success, -1 failure; rejects malformed databases like infiltrate. Release with free_library; prefer infiltrate.

[Declaration](../include/zune.h#L871); [implementation index](TOC.md#public-api).

### zune_free_library

```c
ZUNE_API void zune_free_library(ZuneLibrary *lib);
```

Release a deprecated ZuneLibrary and its owned arrays/strings.

[Declaration](../include/zune.h#L873); [implementation index](TOC.md#public-api).

### zune_finalize

```c
ZUNE_API int zune_finalize(ZuneDeviceHandle dev);
```

Issue 0x9201, 0x9108 and 0x9202 re-index-related requests. Returns -1 for NULL, otherwise 0 regardless of individual operation results. Verify persistence after device re-index.

[Declaration](../include/zune.h#L880); [implementation index](TOC.md#public-api).

### zune_decode_filename

```c
ZUNE_API int zune_decode_filename(const char *filepath,
                               char **out_series, int *out_season,
                               int *out_episode);
```

Heuristically parse series/season/episode from a path. 1 pattern found, 0 absent/invalid. Caller frees allocated series. Not an online identity resolver.

[Declaration](../include/zune.h#L887); [implementation index](TOC.md#public-api).

### zune_snap_thumb

```c
ZUNE_API int zune_snap_thumb(const char *video_path, const char *output_path);
```

Use ffmpeg (ZUNE_FFMPEG/PATH) to grab a frame at 10 seconds, or from the start of a shorter clip, and scale to 200px width. 0 success, -1 failure. Input/output aliases (including symbolic and hard links) are refused without modifying either path. Otherwise an existing file at output_path is replaced, and removed on failure.

[Declaration](../include/zune.h#L895); [implementation index](TOC.md#public-api).

### zune_probe

```c
ZUNE_API int zune_probe(const char *filepath, ZuneMetadata *out);
```

Inspect tags using native libavformat (USE_LIBAV) or the ffprobe fallback (ZUNE_FFPROBE/PATH). 0 inspected, -1 failure with zeroed output. Missing tags are not failure. Container nonblank tags precede selected-stream tags. Free an old result before reusing the struct.

[Declaration](../include/zune.h#L919); [implementation index](TOC.md#public-api).

### zune_free_metadata

```c
ZUNE_API void zune_free_metadata(ZuneMetadata *meta);
```

Free allocated fields within the caller's metadata struct, not the struct itself.

[Declaration](../include/zune.h#L920); [implementation index](TOC.md#public-api).

### zune_smuggle_track_ex

```c
ZUNE_API int zune_smuggle_track_ex(ZuneDeviceHandle dev, const char *filepath,
                        const char *title, const char *artist,
                        const char *album, const char *genre,
                        uint16_t tracknumber, uint32_t duration_ms,
                        zune_progress_fn progress, void *userdata,
                        uint32_t *out_item_id);
```

Explicit-metadata audio upload with synchronous byte-progress callback and userdata. 0 success, -1 failure. Do not reenter device I/O in the callback; caller controls worker dispatch.

[Declaration](../include/zune.h#L929); [implementation index](TOC.md#public-api).

### zune_infiltrate

```c
ZUNE_API int zune_infiltrate(ZuneDeviceHandle dev, ZuneDBLibrary **out);
```

Read/parse ZMDB and fall back internally to MTP playlists if none decoded. 0 success, -1 failure. Fails closed: a database whose header, index or walked media descriptors point outside it returns -1 with the reason in get_error, never a partial scan. Owns returned scan; device borrows it for find helpers. Does not prove all object sizes or fields are known.

[Declaration](../include/zune.h#L976); [implementation index](TOC.md#public-api).

### zune_free_scan

```c
ZUNE_API void zune_free_scan(ZuneDBLibrary *lib);
```

Release the scan and every nested array/string. Does not invalidate the borrowed device cache; never use find helpers afterward until another successful scan.

[Declaration](../include/zune.h#L979); [implementation index](TOC.md#public-api).

### zune_dump_raw

```c
ZUNE_API int zune_dump_raw(ZuneDeviceHandle dev, const char *output_path);
```

Write the raw ZMDB response to output_path for research. 0 success, -1 failure, including a response size outside 13 bytes..64 MiB. Output can contain personal library metadata.

[Declaration](../include/zune.h#L985); [implementation index](TOC.md#public-api).

### zune_infiltrate_deep

```c
ZUNE_API int zune_infiltrate_deep(ZuneDeviceHandle dev);
```

Research-only descriptor scan/logging, including sample record dumps. 0 success, -1 failure; logs can contain personal library metadata.

[Declaration](../include/zune.h#L990); [implementation index](TOC.md#public-api).

### zune_get_error

```c
ZUNE_API const char *zune_get_error(void);
```

Borrowed thread-local diagnostic string, or NULL. Every call that can fail clears it on entry, so it describes the most recent such call on this thread; calls that cannot fail leave it unchanged (see Return conventions). Some failure paths only log, and a successful call can leave text from an internal fallback that failed. Read it on the calling thread before the next call.

[Declaration](../include/zune.h#L1000); [implementation index](TOC.md#public-api).
