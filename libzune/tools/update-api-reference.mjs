#!/usr/bin/env node
// Header signatures plus implementation-reviewed contracts. Never reads key data.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const header = fs.readFileSync(path.join(root, 'include/zune.h'), 'utf8');
const blank = s => s.replace(/[^\n]/g, ' ');
const clean = header.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, blank);
const declarations = [...clean.matchAll(/^[A-Za-z_][\w \t*]*?[ \t*]+(zune_\w+)[ \t]*\([^;{}]*?\)\s*;/gm)];
const notes = Object.fromEntries(`
hd_art_validate|Software-only full JPEG/UUID/UTF-8 validation. No USB or network. Inputs remain borrowed. Returns OK or INVALID. Requires native libavcodec JPEG decoding support.
hd_art_serve|Blocking serialized HD catalog session with full initialization, a single READY callback and safe tunnel closure. Configured buffers remain immutable/borrowed until return. Result carries per-asset and biography acknowledgments, PARTIAL for cache/no-fetch, cancellation and safe_to_continue. Never finalize or retry after unsafe return; sever/reconnect. No hosted server, sockets, deletion, GUID replacement or provider fetching. Physical display and persistence remain separate gates.
hd_art_ensure_artist|Reuse one matching artist with exact catalog GUID, or atomically create a fresh tagged artist after complete artist inventory. Refuses ambiguous names, GUID collisions and existing unbound/different GUIDs without writes. Result retains handle/write_attempted/created on failures; never blindly retry or delete. Requires authenticated HD and serialized ownership. Exact readback is required for success.
dossier|Return the running library version as a static "MAJOR.MINOR.PATCH" string built from the LIBZUNE_VERSION_* macros; never free it. Compare with the header macros to detect a mismatched runtime library.
wiretap|Install a process-wide log callback receiving level, tag ("libzune", "ptp", "mtp", ...) and message (no tag, no newline; valid only during the call). NULL restores the default stderr sink printing "[tag] message". Called on the logging thread, possibly concurrently; it may log or replace the tap. Set before opening a device. A message already being delivered when the tap changes still reaches the old callback, so keep its ctx valid until every device is closed.
set_chatter|Set the process-wide minimum level reaching the sink: WHISPER debug detail, TALK lifecycle (default), SHOUT retries/fallbacks, SCREAM failures, SILENT nothing. Out-of-range values are ignored. The initial level comes from ZUNE_CHATTER (whisper|talk|shout|scream|silent, any case) when set.
breach|Open USB/session and attempt MTPZ authentication if available. Returns a handle or NULL; a non-NULL handle does not prove authentication succeeded. No library scan is performed.
sever|Close session/backend and free the handle. NULL is accepted. Caller must first stop/join every operation using it; the internal delay is not synchronization.
go_dark|Process-wide teardown: wipe (volatile overwrite) and free the MTPZ keys that breach loaded once per process, then re-arm loading so a later breach reads them again. Call only after the last sever, with no device open or handshake running; safe when nothing is loaded. Does not touch libgcrypt.
get_name|Borrowed friendly-name string, or NULL for a NULL handle.
get_model|Borrowed model string, or NULL for a NULL handle.
get_serial|Borrowed serial string, or NULL for a NULL handle.
get_battery|Connect-time cached battery percentage; zero for a NULL handle or unavailable reading.
get_capacity|Cached total storage bytes; refreshed by refresh_storage. Zero for NULL handle.
get_headroom|Cached free storage bytes; refreshed by refresh_storage. Zero for NULL handle.
refresh_storage|Read live storage info, update total/free cache on success. 0 success, -1 failure; failure preserves old cached values.
identify|Return model/profile family inferred from device family/model data, or UNKNOWN. HD 720p is a caller-selected profile rather than a separate device family.
get_family|Return cached device family or UNKNOWN when unavailable.
is_hdd|Return 1 for Keel/Draco, otherwise 0; unknown is not evidence of flash storage.
rename|Set the device friendly name. 0 success, -1 failure.
is_live|Return 1 if local handle/backend state looks connected, else 0. Does not probe USB responsiveness.
abort|Request cooperative cancellation. Wait for the active operation to return before teardown; cancellation does not promise rollback of device objects.
is_aborted|Return the cancellation flag, or 0 for NULL.
clear_abort|Clear cancellation before a new operation, after the old operation has finished.
autopsy|Return the last PTP response code, including synthetic transport failure 0x02FF; 0 for NULL/no response. After a failed track, photo or video upload it names the failed send, not the orphan cleanup that followed. Subsequent operations can replace it.
autopsy_name|Return a borrowed static name for a PTP response code, including a fallback for unknown codes.
unjam|Ask the backend to clear both USB endpoints. 0 success, -1 failure; not a guarantee that the device transaction was rolled back or can be retried.
forge_folder|Create a folder in primary storage under parent_id. Return nonzero ID or 0 on failure.
get_folders|Enumerate folder ID/name entries. Return count (0 for a device with no folders) or -1 with get_error set when the folder query fails or the device stops answering partway through; a folder whose details cannot be read is left out. Release the allocated output using free_folders.
free_folders|Release folder entry names and their array with the returned count.
get_tracks|Enumerate tracks through MTP into an owned array and count. NULL with count 0 is an empty library, or a failed handle query when get_error was set by this call; unreadable objects can be skipped.
free_tracks|Release a separately returned track array and strings. Do not use on a subarray owned by a scan you will also free.
smuggle_track|Upload audio bytes as-is. File tags are not read: the title is the filename without extension and the artist "Unknown Artist"; use smuggle_track_tagged for metadata. Does not transcode or create the album/artist hierarchy. 0 success, -1 failure; optional output receives ID on success.
smuggle_track_tagged|Upload audio using explicit title/artist/album/genre/track/duration values. Does not transcode. 0 success, -1 failure. Streams the file; a failed payload removes the partial object and retries up to three attempts unless the device refused on purpose; an object that cannot be removed is named in get_error and ends the retries.
purge_track|Delete one device object. 0 success, -1 failure; caller must ensure the ID is the intended track.
extract_track|Download an object to dest_path. 0 success, -1 failure; inspect/remove partial output after failure as appropriate.
get_track_state|Read requested playcount/rating. 0 if at least one was read, -1 otherwise; failed fields become zero and skip count always becomes zero. Output pointers may be NULL.
set_track_state|Write playcount and rating (0 neutral, 8 liked, 3 disliked). 0 if both writes succeed, -1 if either fails; partial updates are possible.
verify|Read back size/title/artist and compare. NULL text skips that check; size 0 skips size. 0 if enabled checks pass, -1 otherwise. Does not compare file content or playback.
rename_item|Set object Name (display title), preserving ObjectFileName. 0 success, -1 failure. Does not retag the media file.
sync_notify|Best-effort on-device batch notification. op_kind 0 upload, 1 download; item_index is 1-based, progress fields are percentages 0..100. 0 success, -1 failure.
probe_object|GetObjectInfo existence/size/format probe. Optional size/format outputs. 0 success, -1 failure; transport failure is not proof the object does not exist.
probe_object_named|Object probe plus allocated ObjectFileName, when requested/available. Caller frees name. 0 success, -1 failure.
get_item_refs|Read object reference IDs into an allocated array and count. Caller frees the array. 0 success, -1 failure.
find_track|Search the live borrowed scan snapshot by case-insensitive title+artist+album. Return first ID or 0. No disc/track/content matching; keep scan allocated.
find_video|Search the borrowed scan's legacy filename field case-insensitively; that field can be a ZMDB display title. Return first ID or 0. Keep scan allocated.
find_photo|Search the borrowed scan's photo filename case-insensitively. Return first ID or 0. Keep scan allocated; no folder/content disambiguation.
get_videos|Enumerate videos via MTP into an owned array and count. Explicit title/ObjectFileName fields distinguish display from transport names. NULL can mean empty/error.
free_videos|Release a separately allocated video array, including filename, title and object_filename strings.
smuggle_video_named|Preferred video upload. Independent transport filename and title; requires non-NULL out_item_id. 0 complete, 1 media uploaded but metadata/art incomplete, -1 validation/file-transfer failure. For 0/1 returned ID is valid. TV series properties apply only for TV genre and nonempty series; season/episode must be nonnegative. Never reupload for result 1.
smuggle_movie|Legacy movie upload; display_name is used for transport/display. 0 means media upload succeeded, even if later metadata/poster failed. Prefer smuggle_video_named.
get_series_info|Read series/season/episode and optional title (Name, then filename fallback). 0 when nonempty series found, -1 otherwise. Caller frees returned series/title strings, including outputs returned on a failed series lookup.
smuggle_episode|Legacy TV upload; synthesizes Series - SxxEyy title and only sets series data for positive episode. 0 does not certify metadata/art. Prefer smuggle_video_named.
smuggle_clip|Legacy music-video upload; display_name used for transport/display. 0 does not certify metadata/art. Prefer smuggle_video_named.
smuggle_other|Legacy Other video upload; display_name used for transport/display. 0 does not certify metadata/art. Prefer smuggle_video_named.
purge_video|Delete a video object. 0 success, -1 failure with get_error naming the device response.
extract_video|Download a video object to dest_path. 0 success, -1 failure with get_error set. No direct playback API is provided.
get_photos|Enumerate photos via MTP into an allocated array and count. NULL can mean empty/error.
free_photos|Release a separately returned photo array and strings.
get_dimensions|Read width/height MTP properties. Supply both output pointers. 0 success, -1 failure.
get_photo_albums|Group supplied photos by parent folder and resolve names from this device's folder cache (names unknown to it read "Photos"). A NULL album_count returns NULL. Returned folder albums are allocated; input photos remain caller-owned. Use free_photo_albums.
free_photo_albums|Release allocated photo-album names and array.
arm_photo|Prepare a max-480px JPEG via sips (macOS, ZUNE_SIPS) or ffmpeg (Linux, ZUNE_FFMPEG/PATH). Return a unique owned 0600 temporary path under $TMPDIR, or NULL with no file left behind. Unlink the file and free the path.
smuggle_photo|Prepare JPEG automatically and send to a named album folder; NULL album sends to root. If conversion fails, only an original that is already JPEG is sent; anything else fails before device traffic. An album missing from this device's folder cache (per device handle, freed by sever) is looked up on the device before one is created, and an unreadable folder list fails rather than risk a duplicate. 0 success, -1 failure. No uploaded-ID output is exposed.
purge_photo|Delete a photo object. 0 success, -1 failure with get_error naming the device response.
extract_photo|Download photo to dest_path. 0 success, -1 failure with get_error set.
get_playlists|Enumerate playlists and references through MTP. Owned array/count; NULL can mean empty/error. Unreadable entries or references can yield partial results.
free_playlists|Release a separately returned playlist array, names and track-ID arrays.
forge_playlist|Create an abstract playlist with supplied track IDs/order. Return nonzero object ID or 0. If the commit or the track references fail, the new playlist is deleted, 0 is returned, get_error says whether that cleanup worked and autopsy keeps the original failure. Does not upload tracks.
rewire_playlist|Update playlist name and supplied membership/order. 0 success, -1 failure. This is not an automatic append or transaction across both fields.
purge_playlist|Delete the playlist object, not its referenced media. 0 success, -1 failure.
forge_album|Create an abstract music album and set supplied references. Return nonzero ID or 0. If the commit or the track references fail, the new album is deleted, 0 is returned, get_error says whether that cleanup worked and autopsy keeps the original failure. Create/reuse and link artist separately.
rewire_album|Update album names and replace references with the supplied complete list when nonempty. Caller performs any merge. Empty input does not clear references. Name/artist failures only log warnings, so 0 does not certify metadata; -1 reports invalid device/reference failure.
forge_artist|Reuse a live matching artist, otherwise create a zero-byte abstract artist. Return ID or 0; failed inventory blocks creation. ASCII case/outer-space matching preserves UTF-8 bytes; no duplicate cleanup.
link_artist|Write ArtistId for an album/track. 0 success, -1 failure.
get_albums_checked|Read a strict complete album inventory, including names, artist names and references. Return 0 including proven empty, -1 with NULL/zero outputs on any unreadable or malformed object. Caller frees successful arrays with free_albums. No USB writes.
get_albums|Enumerate abstract albums, names, artists and references via MTP. Owned array/count; NULL can mean empty/error; property failures can produce incomplete records.
free_albums|Release album array, strings and track-ID arrays returned by get_albums.
get_podcast_series|Experimental complete MTP series inventory with exact feed identity. Requires successful MTPZ authentication; unauthenticated sessions fail rather than reporting an empty library. 0 success, including NULL array/count 0 when empty; -1 failure with no partial result. Use free_podcast_series. Synchronous, requiring the same serializer as all other device calls. Keel roundtrip tested; other-model acceptance pending.
get_podcast_episodes|Read-only native schema-0x10 episode inventory from complete wildcard handles. Authenticated active Keel/Pavo/Draco sessions only. Both output pointers required; 0 success including NULL/count zero when empty, -1 failure with no partial output. Owned array and title/author/series_name strings use free_podcast_episodes. Group by series_id, never title; firmware may return empty series_name, resolved through series inventory. Exact property widths and Unicode validated, transport failures stop immediately. No writes or playback guarantees.
get_podcast_episode_sources|Read-only complete exact feed/source episode mapping for authenticated active Keel/Pavo/Draco. Reads series once, then each distinct series folder's MP3 inventory once; validates exact series relationship, live parent/format and podcast classification. Both outputs required. 0 succeeds including NULL/count 0 when known empty; -1 unavailable, malformed or ambiguous with no partial output. Duplicate feeds, exact feed/source pairs or handles fail closed. Owned array plus UTF-8 feed_url/source_url strings use free_podcast_episode_sources. Same source URL under distinct feeds remains distinct. No title heuristics, mutations or deletion authorization. Serialize all device I/O.
free_podcast_episode_sources|Free owned feed_url/source_url strings and the get_podcast_episode_sources array using the returned count. NULL accepted.
get_podcast_states|Read fresh persisted podcast state from a complete bounded ZMDB snapshot. Requires authenticated active Keel/Draco with v2 or Pavo with v5; serialize device I/O. Both output pointers required. 0 means complete success, including NULL/count 0 when known empty; -1 means unavailable, malformed, canceled or incomplete and returns NULL/count 0. Caller owns array, released by free_podcast_states. Exposes exact atom/series IDs, owned validated UTF-8 title, duration, bookmark, raw flags and marked_played mask 0x200. Bookmark and played are independent. This records firmware state, not proof of uninterrupted natural completion, and never authorizes deletion. Join fresh live identities including exact title and duration before a separate policy acts; unknown state must preserve media.
free_podcast_states|Release the owned get_podcast_states array and its title strings using the returned count. NULL accepted.
free_podcast_episodes|Free a get_podcast_episodes array and all its strings with the returned count. NULL accepted.
get_audiobook_parts|Read-only native schema-0x12 book-part inventory from complete wildcard handles, including HD objects omitted by format-filtered queries. Same authentication, family, required-output and all-or-nothing contract as get_podcast_episodes. Group by parent_id, never book_title; sort by part_number in the caller. Owned array and title/author/book_title strings use free_audiobook_parts. No format conversion or device writes.
free_audiobook_parts|Free a get_audiobook_parts array and all its strings with the returned count. NULL accepted.
free_podcast_series|Free the owned series array and each name/author/feed string using its returned count. NULL is accepted.
forge_podcast_series|Experimental zero-byte podcast series creation under an explicit parent, or reuse of exactly one live exact feed-URL match. Requires successful MTPZ authentication. Never merges by title or overwrites existing metadata. Descriptor and property readback checks. Required out_id is zeroed first; 0 return succeeds, negative fails. ZUNE_PODCAST_FAILED_CLEANED (-2) confirms deletion of the new object; other failures return -1. A nonzero ID on failure identifies only a new orphan whose cleanup failed: inspect before retrying. No implicit artwork or finalize. Keel roundtrip tested; other-model acceptance pending.
smuggle_podcast_episode|Experimental caller-prepared MP3 episode upload, with podcast category and series relationship. Requires successful MTPZ authentication; no transcoding or codec sniffing. Live series/source-URL identity within the supplied parent folder plus title/size checks controls reuse; ambiguous matches fail without writes. Caller retains feed/GUID identity across URL changes. Firmware may normalize filenames. Keel/Pavo/Draco schema 0x10 supports zero MetaGenre/empty redundant SeriesName readback; series link/URL/title and format/size must match. 0 success, negative failure (including ZUNE_PODCAST_FAILED_CLEANED), required out_id uses the same orphan contract as forge_podcast_series. Uploads through the shared engine: a failed payload removes the new episode and retries up to three attempts; an unanswered creation is never retried; FAILED_CLEANED means every episode this call created was removed. No implicit finalize or played/bookmark guarantee. Keel upload/reuse/extraction/cleanup and new-session repeat tested; user confirmed Classic playback and show artwork. Resume and other models remain unverified.
find_podcast_episode|Read-only queue admission lookup by exact feed URL and episode source URL. Requires MTPZ authentication. Returns 0 for a complete lookup (zero IDs mean absent), -1 for failure with zeroed outputs. Returns the live series parent, series ID and episode ID; ambiguous series/episodes, unreadable inventory/links and wrong classification fail closed. Does not compare title or byte size, download audio, create or rewrite objects. Caller persists feed/GUID-to-source identity and handles changed downloads explicitly.
find_audiobook_part|Experimental read-only live lookup by caller-provided book content key and positive part number. Requires authentication and tested Keel/Pavo/Draco family. Validates book/title/author/duration/category and refuses conflicting same-title books in other folders. 0 complete, -1 failure; required folder/part outputs zero on failure, zero part means absent. No ZMDB cache or writes.
smuggle_audiobook_part|Experimental prepared immutable MP3 upload under an identity-named folder inside OverDrive. No music artist/album/reference creation. Reuses live book/part identity plus metadata and size; descriptors checked before creation. 0 success, -1 failure. Uploads through the shared engine: a failed payload removes the new part and retries up to three attempts; an unanswered creation is never retried. Required output retains a part ID on failure only when that part could not be removed or failed readback after upload; lost responses can leave uncertain state even with ID zero. No codec sniffing, conversion, chapter splitting, artwork or finalization. Serialized authenticated Keel/Pavo/Draco only; long files and resume remain separate gates.
brand|Write JPEG representative-sample artwork to an object. 0 success, -1 failure; image is not automatically resized by this API.
get_brand_limits|Read the maximum artwork width/height (pixels) and JPEG size (bytes) the device declares for the item's object format; 0 means not declared. Reads ObjectInfo, then the format's descriptors once per connection (cached until sever). Unsupported properties are 0; a transport failure or malformed descriptor returns -1 with all outputs 0 and get_error set. Firmware can store larger images than it declares yet not display them; resize before brand.
grab_thumb|Try representative sample then MTP thumbnail, writing cache_path. 0 success, -1 failure. Does not download the complete media file.
grab_photo_thumb|Photo-only fallback: representative sample, MTP thumbnail, then complete photo download/resize. 0 success, -1 failure. Do not use as a video thumbnail API.
arm_audio|Legacy MP3 320kbps/ID3v2.3 preparation, with source metadata stripped for separate MTP tagging. Runs ffmpeg found via ZUNE_FFMPEG/PATH. Owned temporary path or NULL; unlink and free.
retag|Legacy MP3 stream-copy retag to ID3v2.3. Runs ffmpeg found via ZUNE_FFMPEG/PATH. Owned temporary path or NULL; unlink and free.
arm_video|Legacy transcode by ZuneModel profile (WMV for 30, H.264 for other supported profiles). Runs ffmpeg found via ZUNE_FFMPEG/PATH. Owned temporary path or NULL; unlink and free.
arm_episode|Legacy video preparation with series/season/episode container metadata. Same tool lookup and temp ownership as arm_video.
infiltrate_legacy|Deprecated tracks/videos/photos-only ZMDB result. 0 success, -1 failure; rejects malformed databases like infiltrate. Release with free_library; prefer infiltrate.
free_library|Release a deprecated ZuneLibrary and its owned arrays/strings.
finalize|Issue 0x9201, 0x9108 and 0x9202 re-index-related requests. Returns -1 for NULL, otherwise 0 regardless of individual operation results. Verify persistence after device re-index.
decode_filename|Heuristically parse series/season/episode from a path. 1 pattern found, 0 absent/invalid. Caller frees allocated series. Not an online identity resolver.
snap_thumb|Use ffmpeg (ZUNE_FFMPEG/PATH) to grab a frame at 10 seconds, or from the start of a shorter clip, and scale to 200px width. 0 success, -1 failure. Input/output aliases (including symbolic and hard links) are refused without modifying either path. Otherwise an existing file at output_path is replaced, and removed on failure.
probe|Inspect tags using native libavformat (USE_LIBAV) or the ffprobe fallback (ZUNE_FFPROBE/PATH). 0 inspected, -1 failure with zeroed output. Missing tags are not failure. Container nonblank tags precede selected-stream tags. Free an old result before reusing the struct.
free_metadata|Free allocated fields within the caller's metadata struct, not the struct itself.
smuggle_track_ex|Explicit-metadata audio upload with synchronous byte-progress callback and userdata. 0 success, -1 failure. Do not reenter device I/O in the callback; caller controls worker dispatch.
infiltrate|Read/parse ZMDB and fall back internally to MTP playlists if none decoded. 0 success, -1 failure. Fails closed: a database whose header, index or walked media descriptors point outside it returns -1 with the reason in get_error, never a partial scan. Owns returned scan; device borrows it for find helpers. Does not prove all object sizes or fields are known.
free_scan|Release the scan and every nested array/string. Does not invalidate the borrowed device cache; never use find helpers afterward until another successful scan.
dump_raw|Write the raw ZMDB response to output_path for research. 0 success, -1 failure, including a response size outside 13 bytes..64 MiB. Output can contain personal library metadata.
infiltrate_deep|Research-only descriptor scan/logging, including sample record dumps. 0 success, -1 failure; logs can contain personal library metadata.
get_error|Borrowed thread-local diagnostic string, or NULL. Every call that can fail clears it on entry, so it describes the most recent such call on this thread; calls that cannot fail leave it unchanged (see Return conventions). Some failure paths only log, and a successful call can leave text from an internal fallback that failed. Read it on the calling thread before the next call.
`.trim().split('\n').map(row => { const i = row.indexOf('|'); return [`zune_${row.slice(0, i)}`, row.slice(i + 1)]; }));
// Return convention and whether the call clears zune_get_error on entry (T3.5).
// tests/run-error-convention.sh checks the source against the generated table.
const conventions = Object.fromEntries(`
hd_art_validate|HD result: ZUNE_HD_ART_OK or ZUNE_HD_ART_INVALID|yes
hd_art_serve|HD result: a ZuneHDArtStatus (negative on failure)|yes
hd_art_ensure_artist|HD result: a ZuneHDArtStatus (negative on failure)|yes
dossier|pointer: static version string, never NULL|no
wiretap|none|no
set_chatter|none|no
breach|pointer-or-NULL: device handle, or NULL|yes
sever|none|no
go_dark|none|no
get_name|pointer-or-NULL: borrowed cached string; NULL for a NULL handle|no
get_model|pointer-or-NULL: borrowed cached string; NULL for a NULL handle|no
get_serial|pointer-or-NULL: borrowed cached string; NULL for a NULL handle|no
get_battery|value: cached percentage; 0 when unavailable|no
get_capacity|value: cached bytes; 0 for a NULL handle|no
get_headroom|value: cached bytes; 0 for a NULL handle|no
refresh_storage|status|yes
identify|value: a ZuneModel; UNKNOWN when unavailable|no
get_family|value: a ZuneDeviceFamily; UNKNOWN when unavailable|no
is_hdd|boolean: 1 or 0|no
rename|status|yes
is_live|boolean: 1 or 0|no
abort|none|no
is_aborted|boolean: 1 or 0|no
clear_abort|none|no
autopsy|value: last PTP response code; 0 when none|no
autopsy_name|pointer: static name, never NULL|no
unjam|status|yes
forge_folder|handle-or-0|yes
get_folders|count-or-error: folder count, or ZUNE_BLOWN|yes
free_folders|none|no
get_tracks|pointer-or-NULL: owned array; NULL when empty or failed|yes
free_tracks|none|no
smuggle_track|status|yes
smuggle_track_tagged|status|yes
purge_track|status|yes
extract_track|status|yes
get_track_state|status|yes
set_track_state|status|yes
verify|status|yes
rename_item|status|yes
sync_notify|status|yes
probe_object|status|yes
probe_object_named|status|yes
get_item_refs|status|yes
find_track|handle-or-0: 0 when absent|yes
find_video|handle-or-0: 0 when absent|yes
find_photo|handle-or-0: 0 when absent|yes
get_videos|pointer-or-NULL: owned array; NULL when empty or failed|yes
free_videos|none|no
smuggle_video_named|status: ZUNE_OK, ZUNE_VIDEO_METADATA_INCOMPLETE (1) or ZUNE_BLOWN|yes
smuggle_movie|status|yes
get_series_info|status|yes
smuggle_episode|status|yes
smuggle_clip|status|yes
smuggle_other|status|yes
purge_video|status|yes
extract_video|status|yes
get_photos|pointer-or-NULL: owned array; NULL when empty or failed|yes
free_photos|none|no
get_dimensions|status|yes
get_photo_albums|pointer-or-NULL: owned array; NULL when empty or failed|yes
free_photo_albums|none|no
arm_photo|pointer-or-NULL: owned temporary path|yes
smuggle_photo|status|yes
purge_photo|status|yes
extract_photo|status|yes
get_playlists|pointer-or-NULL: owned array; NULL when empty or failed|yes
free_playlists|none|no
forge_playlist|handle-or-0|yes
rewire_playlist|status|yes
purge_playlist|status|yes
forge_album|handle-or-0|yes
rewire_album|status|yes
forge_artist|handle-or-0|yes
link_artist|status|yes
get_albums|pointer-or-NULL: owned array; NULL when empty or failed|yes
get_albums_checked|status|yes
free_albums|none|no
get_podcast_series|status|yes
free_podcast_series|none|no
get_podcast_episodes|status|yes
free_podcast_episodes|none|no
get_podcast_episode_sources|status|yes
free_podcast_episode_sources|none|no
get_podcast_states|status|yes
free_podcast_states|none|no
forge_podcast_series|status: ZUNE_OK, ZUNE_PODCAST_FAILED_CLEANED (-2) or ZUNE_BLOWN; ID through out_id|yes
smuggle_podcast_episode|status: ZUNE_OK, ZUNE_PODCAST_FAILED_CLEANED (-2) or ZUNE_BLOWN; ID through out_id|yes
find_podcast_episode|status|yes
get_audiobook_parts|status|yes
free_audiobook_parts|none|no
find_audiobook_part|status|yes
smuggle_audiobook_part|status|yes
brand|status|yes
get_brand_limits|status|yes
grab_thumb|status|yes
grab_photo_thumb|status|yes
arm_audio|pointer-or-NULL: owned temporary path|yes
retag|pointer-or-NULL: owned temporary path|yes
arm_video|pointer-or-NULL: owned temporary path|yes
arm_episode|pointer-or-NULL: owned temporary path|yes
infiltrate_legacy|status|yes
free_library|none|no
finalize|status|yes
decode_filename|boolean: 1 when a pattern matched, else 0|yes
snap_thumb|status|yes
probe|status|yes
free_metadata|none|no
smuggle_track_ex|status|yes
infiltrate|status|yes
free_scan|none|no
dump_raw|status|yes
infiltrate_deep|status|yes
get_error|pointer-or-NULL: borrowed thread-local message|no
`.trim().split('\n').map(row => { const [n, c, k] = row.split('|'); return [`zune_${n}`, { convention: c, clears: k }]; }));
const names = new Set(declarations.map(m => m[1]));
const missing = [...names].filter(n => !notes[n]);
const extra = Object.keys(notes).filter(n => !names.has(n));
if (missing.length || extra.length) throw Error(`Contract coverage mismatch: missing ${missing}; extra ${extra}`);
const missingConv = [...names].filter(n => !conventions[n]);
const extraConv = Object.keys(conventions).filter(n => !names.has(n));
if (missingConv.length || extraConv.length)
    throw Error(`Convention coverage mismatch: missing ${missingConv}; extra ${extraConv}`);
const returnType = match => header.slice(match.index, match.index + match[0].length)
    .split(match[1])[0].replace(/\bZUNE_API\b/, '').replace(/\s+/g, ' ').trim();
const deprecation = match => {
    const d = header.slice(match.index, match.index + match[0].length).match(/ZUNE_DEPRECATED\("([^"]*)"\)/);
    return d ? d[1] : null;
};
for (const match of declarations) {
    const { convention, clears } = conventions[match[1]];
    const type = returnType(match);
    const kind = convention.split(':')[0];
    const ok = { 'status': type === 'int', 'count-or-error': type === 'int', 'handle-or-0': type === 'uint32_t',
                 'pointer-or-NULL': /\*$|Handle$/.test(type), 'pointer': /\*$/.test(type), 'none': type === 'void',
                 'boolean': type === 'int', 'value': !/\*$/.test(type) && type !== 'void',
                 'HD result': type === 'ZuneHDArtStatus' }[kind];
    if (!ok) throw Error(`${match[1]}: convention "${convention}" does not fit return type ${type}`);
    if (clears !== 'yes' && clears !== 'no') throw Error(`${match[1]}: clears must be yes or no`);
}
const target = path.join(root, 'docs/API_REFERENCE.md');
const existing = fs.readFileSync(target, 'utf8');
const marker = '<!-- GENERATED API START -->';
if (!existing.includes(marker)) throw Error('Missing API generation marker');
const intro = existing.split(marker)[0].replace(/Current public interface: \*\*\d+ functions\*\*/,
    `Current public interface: **${declarations.length} functions**`);
let result = intro + marker + '\n\n## Return conventions\n\n';
result += 'Every public function in header order. *status* means `ZUNE_OK` (0) on success and `ZUNE_BLOWN` (-1) ' +
    'on failure. The last column says whether the call clears `zune_get_error` on entry; calls that cannot ' +
    'fail leave it unchanged.\n\n| Function | Returns | Convention | Clears error |\n|---|---|---|---|\n';
for (const match of declarations) {
    const { convention, clears } = conventions[match[1]];
    const replacement = deprecation(match);
    const text = convention.replace(/\b(ZUNE_[A-Z_]+|Zune[A-Za-z]+|out_id)\b/g, '`$1`') +
        (replacement ? `; deprecated, ${replacement.replace(/\b(zune_\w+)/g, '`$1`')}` : '');
    result += `| \`${match[1]}\` | \`${returnType(match)}\` | ${text} | ${clears} |\n`;
}
result += '\n## Complete function reference\n\n';
result += `${declarations.length} declarations, in public-header order.\n\n`;
for (const match of declarations) {
    const signature = header.slice(match.index, match.index + match[0].length).trim();
    const line = header.slice(0, match.index).split('\n').length;
    result += `### ${match[1]}\n\n\`\`\`c\n${signature}\n\`\`\`\n\n${notes[match[1]]}\n\n[Declaration](../include/zune.h#L${line}); [implementation index](TOC.md#public-api).\n\n`;
}
result = result.trimEnd() + '\n';
if (process.argv.includes('--check')) {
    if (existing !== result) { console.error('API reference stale; run node tools/update-api-reference.mjs'); process.exit(1); }
    console.log(`API reference current: ${declarations.length} exact declarations and reviewed contracts.`);
} else {
    fs.writeFileSync(target, result);
    console.log(`API reference updated: ${declarations.length} functions.`);
}
