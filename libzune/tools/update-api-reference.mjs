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
breach|Open USB/session and attempt MTPZ authentication if available. Returns a handle or NULL; a non-NULL handle does not prove authentication succeeded. No library scan is performed.
sever|Close session/backend and free the handle. NULL is accepted. Caller must first stop/join every operation using it; the internal delay is not synchronization.
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
autopsy|Return the last PTP response code, including synthetic transport failure 0x02FF; 0 for NULL/no response. Subsequent operations can replace it.
autopsy_name|Return a borrowed static name for a PTP response code, including a fallback for unknown codes.
unjam|Ask the backend to clear both USB endpoints. 0 success, -1 failure; not a guarantee that the device transaction was rolled back or can be retried.
forge_folder|Create a folder in primary storage under parent_id. Return nonzero ID or 0 on failure.
get_folders|Enumerate folder ID/name entries. Return count or -1; release the allocated output using free_folders.
free_folders|Release folder entry names and their array with the returned count.
get_tracks|Enumerate tracks through MTP into an owned array and count. NULL can mean empty/error; unreadable objects can be skipped.
free_tracks|Release a separately returned track array and strings. Do not use on a subarray owned by a scan you will also free.
smuggle_track|Probe source metadata, then upload audio bytes. Does not transcode or create the album/artist hierarchy. 0 success, -1 failure; optional output receives ID on success.
smuggle_track_tagged|Upload audio using explicit title/artist/album/genre/track/duration values. Does not transcode. 0 success, -1 failure.
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
purge_video|Delete a video object. 0 success, -1 failure.
extract_video|Download a video object to dest_path. 0 success, -1 failure. No direct playback API is provided.
get_photos|Enumerate photos via MTP into an allocated array and count. NULL can mean empty/error.
free_photos|Release a separately returned photo array and strings.
get_dimensions|Read width/height MTP properties. Supply both output pointers. 0 success, -1 failure.
get_photo_albums|Group supplied photos by parent folder and resolve names. Returned folder albums are allocated; input photos remain caller-owned. Use free_photo_albums.
free_photo_albums|Release allocated photo-album names and array.
arm_photo|Prepare a max-480px JPEG via sips (macOS) or ffmpeg on PATH (Linux). Return owned temporary path or NULL. Unlink the file and free the path; serialize calls to avoid temporary-name collisions.
smuggle_photo|Prepare JPEG automatically and send to a named album folder, creating it when absent; NULL album sends to root. 0 success, -1 failure. No uploaded-ID output is exposed.
purge_photo|Delete a photo object. 0 success, -1 failure.
extract_photo|Download photo to dest_path. 0 success, -1 failure.
get_playlists|Enumerate playlists and references through MTP. Owned array/count; NULL can mean empty/error. Unreadable entries or references can yield partial results.
free_playlists|Release a separately returned playlist array, names and track-ID arrays.
forge_playlist|Create an abstract playlist with supplied track IDs/order. Return nonzero object ID or 0. Does not upload tracks.
rewire_playlist|Update playlist name and supplied membership/order. 0 success, -1 failure. This is not an automatic append or transaction across both fields.
purge_playlist|Delete the playlist object, not its referenced media. 0 success, -1 failure.
forge_album|Create an abstract music album and set supplied references. Return nonzero ID or 0. Create/reuse and link artist separately; inspect logs/readback for ancillary metadata/reference failures.
rewire_album|Update album names and replace references with the supplied complete list when nonempty. Caller performs any merge. Empty input does not clear references. Name/artist failures only log warnings, so 0 does not certify metadata; -1 reports invalid device/reference failure.
forge_artist|Reuse a live matching artist, otherwise create a zero-byte abstract artist. Return ID or 0; failed inventory blocks creation. ASCII case/outer-space matching preserves UTF-8 bytes; no duplicate cleanup.
link_artist|Write ArtistId for an album/track. 0 success, -1 failure.
get_albums|Enumerate abstract albums, names, artists and references via MTP. Owned array/count; NULL can mean empty/error; property failures can produce incomplete records.
free_albums|Release album array, strings and track-ID arrays returned by get_albums.
brand|Write JPEG representative-sample artwork to an object. 0 success, -1 failure; image is not automatically resized by this API.
grab_thumb|Try representative sample then MTP thumbnail, writing cache_path. 0 success, -1 failure. Does not download the complete media file.
grab_photo_thumb|Photo-only fallback: representative sample, MTP thumbnail, then complete photo download/resize. 0 success, -1 failure. Do not use as a video thumbnail API.
arm_audio|Legacy MP3 320kbps/ID3v2.3 preparation, with source metadata stripped for separate MTP tagging. Hard-coded Homebrew ffmpeg path. Owned temporary path or NULL; unlink and free.
retag|Legacy MP3 stream-copy retag to ID3v2.3. Hard-coded Homebrew ffmpeg path. Owned temporary path or NULL; unlink and free.
arm_video|Legacy transcode by ZuneModel profile (WMV for 30, H.264 for other supported profiles). Hard-coded Homebrew ffmpeg path. Owned temporary path or NULL; unlink and free.
arm_episode|Legacy video preparation with series/season/episode container metadata. Same portability/temp ownership limits as arm_video.
infiltrate_legacy|Deprecated tracks/videos/photos-only ZMDB result. 0 success, -1 failure. Release with free_library; prefer infiltrate.
free_library|Release a deprecated ZuneLibrary and its owned arrays/strings.
finalize|Issue 0x9201, 0x9108 and 0x9202 re-index-related requests. Returns -1 for NULL, otherwise 0 regardless of individual operation results. Verify persistence after device re-index.
decode_filename|Heuristically parse series/season/episode from a path. 1 pattern found, 0 absent/invalid. Caller frees allocated series. Not an online identity resolver.
snap_thumb|Use ffmpeg from PATH to grab a frame at 10 seconds and scale to 200px width. 0 success, -1 failure; short media may have no frame there.
probe|Inspect tags using native libavformat (USE_LIBAV) or ffprobe fallback. 0 inspected, -1 failure with zeroed output. Missing tags are not failure. Container nonblank tags precede selected-stream tags. Free an old result before reusing the struct.
free_metadata|Free allocated fields within the caller's metadata struct, not the struct itself.
smuggle_track_ex|Explicit-metadata audio upload with synchronous byte-progress callback and userdata. 0 success, -1 failure. Do not reenter device I/O in the callback; caller controls worker dispatch.
infiltrate|Read/parse ZMDB and fall back internally to MTP playlists if none decoded. 0 success, -1 failure. Owns returned scan; device borrows it for find helpers. Does not prove all object sizes or fields are known.
free_scan|Release the scan and every nested array/string. Does not invalidate the borrowed device cache; never use find helpers afterward until another successful scan.
dump_raw|Write the raw ZMDB response to output_path for research. 0 success, -1 failure. Output can contain personal library metadata.
infiltrate_deep|Research-only descriptor scan/logging, including sample record dumps. 0 success, -1 failure; logs can contain personal library metadata.
get_error|Borrowed thread-local diagnostic string. Read on the failing call's thread; not all error paths update it and successful operations need not clear it.
`.trim().split('\n').map(row => { const i = row.indexOf('|'); return [`zune_${row.slice(0, i)}`, row.slice(i + 1)]; }));
const names = new Set(declarations.map(m => m[1]));
const missing = [...names].filter(n => !notes[n]);
const extra = Object.keys(notes).filter(n => !names.has(n));
if (missing.length || extra.length) throw Error(`Contract coverage mismatch: missing ${missing}; extra ${extra}`);
const target = path.join(root, 'docs/API_REFERENCE.md');
const existing = fs.readFileSync(target, 'utf8');
const marker = '<!-- GENERATED API START -->';
if (!existing.includes(marker)) throw Error('Missing API generation marker');
let result = existing.split(marker)[0] + marker + '\n\n## Complete function reference\n\n';
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
