# Video display titles and transport filenames

Implemented 2026-09-07. The hardware verification result belongs below only
after an actual device run; the transport fixture is a software gate.

## Wire evidence

The saved Windows USB capture in
[WIRE_CAPTURE_FINDINGS.md, video properties](WIRE_CAPTURE_FINDINGS.md#captured-recipes-property-order-as-sent)
records these independent properties for an episode:

| Property | Captured value |
|---|---|
| ObjectFileName `0xDC07` | `Cyberpunk Edgerunners - S01E01 - Let You Down.mp4` |
| Name `0xDC44` | `Let You Down` |
| SeriesName `0xDA9A` | `Cyberpunk Edgerunners` |
| Season `0xDAB5` | `1` as UINT32 |
| Episode `0xDAB6` | `1` as UINT32 |

The old app passed its formatted transport filename to the single
`display_name` argument. Movie uploads therefore wrote the extension into
Name. The legacy episode API also explicitly synthesized Name as
`Series - SxxEyy`; that behavior was library code, not evidence that firmware
rewrites episode titles.

## New API and completion semantics

`zune_smuggle_video_named(dev, filepath, object_filename, title, meta_genre,
series, season, episode, description, poster_jpeg, poster_len, out_item_id)`
handles Movie, TV Show, Music Video, and Other. The filename includes its
playable extension; the title is written to Name without decoration. The
prepared file extension determines the MTP format as before. TV vendor fields
are written independently when a nonempty series is supplied, including zero
season/episode values for specials. Non-TV categories do not acquire TV fields.

Description remains AUINT16 and poster data remains AUINT8. The function uses
the existing SendObjectInfo + SendObject + property-setter transport. This
change does not migrate media creation to the Windows client's atomic
SendObjectPropList recipe; that remains a separate protocol change.

| Result | Meaning | Caller action |
|---|---|---|
| `0` | Media upload and every requested metadata/art setter succeeded | Use actual returned ID; perform application readback verification |
| `ZUNE_VIDEO_METADATA_INCOMPLETE` (`1`) | Media is fully uploaded; one or more metadata/art writes failed | Preserve returned ID; report `zune_get_error()`; **do not upload again** |
| `-1` | Invalid input or file upload failed | Output ID is zero; no complete upload is reported |

The error identifies the failed metadata fields. No post-upload metadata
failure triggers deletion or a second file upload. Setter success alone is
not a firmware display test. Send failure triage preserves the original PTP
response across partial-object cleanup so a successful DeleteObject cannot
turn a fatal refusal into a retry.

The existing movie/episode/clip/other APIs retain their source signatures and
legacy title behavior. Callers opt into the new API explicitly.

## Readback and ownership

`ZuneVideoFile` now appends `title`, `object_filename`, and `object_format`.
Rebuild both library and consumers; this extends the C struct layout.

| Field | ZMDB scan | MTP fallback enumeration |
|---|---|---|
| Legacy `filename` | Name from the record's UTF-8 title | ObjectInfo filename |
| `title` | Name from the record's UTF-8 title | Name `0xDC44`, empty if unavailable |
| `object_filename` | Empty: not decoded from this record | ObjectInfo filename `0xDC07` |
| `object_format` | Format code at record offset `+32`, or zero for a short record | ObjectInfo format code |

Keeping the legacy field unchanged preserves existing Mac source behavior;
new consumers must use the explicit fields. All three strings are owned by
the result. `zune_free_videos`, `zune_free_scan`, and `zune_free_library` release
them. Never infer a filename or playable extension from a clean display title.

`zune_get_series_info` returns Name as `out_title`, with ObjectFileName as a
fallback when Name is empty or unreadable. Series/season/episode remain
independent. Optional output pointers are supported.

Existing content can be repaired by `zune_rename_item`, which sets only Name.
It does not rename ObjectFileName or rewrite series, season, episode, or media
bytes. Select existing IDs using authoritative app metadata; never use a
title collision as permission to rename an arbitrary device object.

## Verification

Run the isolated software gate:

```sh
bash tests/run-video-named.sh
```

This compiles actual `video.c`, `mtp.c`, `finalize.c`, and the ZMDB record
parser, replacing only the PTP transaction boundary and connection/error
state. No USB backend is linked. Twenty-six checks exercise exact packed
ObjectFileName and Name, Unicode title round-trip, category and TV fields,
description/poster encodings, Name-only rename, actual readback, all seven
metadata failure types, complete-upload ID preservation, transfer failures,
legacy behavior, and both WMV/MP4 ZMDB format values. AddressSanitizer,
UndefinedBehaviorSanitizer, and LeakSanitizer passed. LeakSanitizer needs an
environment without ptrace restrictions.

For an explicitly authorized hardware gate with exclusive device ownership:

```sh
make zunetool
./zunetool video-title-test /path/to/short-fixture.wmv
```

The command rejects non-WMV/non-ASF input and files above 1 MiB before
connecting. Supply a valid short playable WMV fixture. It sends one movie and
one episode with unique filenames, verifies Name and ObjectFileName directly,
checks category, size, format, and TV fields, and renames only its own episode
before checking that all other properties remain intact. Every completion or
failure path attempts to delete only the IDs returned by those two uploads,
logs each deletion result, finalizes, and severs. SIGINT/SIGTERM finish the
current small operation before taking that cleanup path. A lost USB transport
can still prevent cleanup; the command reports any unconfirmed handle.

Hardware result: **passed 2026-09-07, Zune 30 (Keel)**. The named-video test used
a 13,699-byte WMV: movie handle 33555819 and episode handle 33555820. Name,
ObjectFileName, category, size/format and episode vendor fields read back exactly.
Name-only rename preserved filename, series, numbering and bytes. Both created
handles were deleted, and CleanDataStore completed successfully. The expected
Keel 0x2005 replies to 0x9201/0x9202 did not affect finalization. Local run log:
`/tmp/zuuned-title-hardware.log`.

No claim about on-device UI display or successful
physical cleanup is made by the software fixture.
