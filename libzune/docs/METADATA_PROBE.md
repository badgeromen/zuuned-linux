# Metadata probe contract — tester release R2

`zune_probe()` reads local media metadata through native libavformat when
`USE_LIBAV` is enabled, as it is in the Linux and macOS builds. The legacy build
without libav uses ffprobe. Neither path calls USB/device functions.

The 2026-09-08 correction fixes Ogg/Vorbis and Opus tags being ignored because
they live on the audio stream. It also prevents a failed probe from reporting
successful empty metadata, and adds release-year and disc-number propagation.

## Public structure and ownership

`ZuneMetadata` appends these fields after the existing `duration_ms` member:

```c
int discnumber; /* positive disc index; 0 means missing/invalid */
int year;       /* year from the date/year tag; 0 means missing/invalid */
```

Existing member offsets are unchanged. The structure is larger: rebuild every
consumer against the matching header and library. This is source compatibility
for rebuilt C/C++ and imported-C consumers, not a binary-compatible replacement
for an old executable using the shared library with the smaller struct.

On success, text fields are owned UTF-8 allocations or `NULL` for missing/blank
tags. The caller uses `zune_free_metadata()`, which frees strings and resets all
members, including the appended fields. Free an earlier successful result before
reusing its output structure. Failed probes leave a fully zeroed/free-safe output;
invalid output pointers return `-1` without dereferencing them.

## Selection and precedence

The selected stream is the first usable audio stream marked default, otherwise
the first usable audio stream. If there is no usable audio stream, the same rule
selects a video stream. Other streams do not contribute tag fragments.

For each field, a nonblank container tag wins. The selected stream fills only
missing fields. Outer ASCII and Unicode whitespace is trimmed; interior spacing,
accents, punctuation and escaped text remain intact. Blank preferred aliases
cannot hide a valid alias. Numeric aliases with malformed values can fall through
to a valid alias or stream value.

| Field | Preferred tag / alias |
|---|---|
| Title, artist, album, genre | `title`, `artist`, `album`, `genre` |
| Album artist | `album_artist`, then `albumartist` |
| Track | `track`, then `tracknumber` |
| Disc | `disc`, then `discnumber` |
| Release year | valid `date`, then valid `year` |

Track/disc tags accept positive decimal `N` or `N/total`, with optional whitespace
around the slash. Totals must be valid and at least N. Track values cannot exceed
`UINT16_MAX`; disc values cannot exceed `INT_MAX`. Negative, malformed and
overflowing values become missing instead of wrapping through `atoi()` casts.

Year parsing accepts `YYYY`, `YYYY-MM`, `YYYY-MM-DD`, slash-separated dates and
valid ISO-style timestamp suffixes. Invalid calendar values and arbitrary
trailing text are rejected. This uses release date/year tags; file timestamps,
container creation time and filename guesses do not become release years.

## Success and failure

Return `0` means the media was inspected and has a usable audio or video stream.
Missing tags and zero duration alone do not imply failure. Audio streams require
a known codec, positive sample rate and channel count; video streams require a
known codec and positive dimensions. This catches arbitrary text renamed `.flac`,
which libav/ffprobe can otherwise accept as a nominal stream with zero channels.

Unreadable files, failed stream inspection and streams without usable parameters
return `-1` with zeroed metadata. Valid video-only files still return their tags
and duration. Container duration is preferred, with selected-stream duration as
fallback; excessive duration saturates at the existing 32-bit millisecond limit.

The fallback checks ffprobe's actual exit status, reads complete lines, preserves
quoted/escaped values and selects the same stream as the native path. Arguments
go directly to the child process, so quotes and shell punctuation in a filename
remain literal. Its stream table is capped at 1,024 entries.

This remains a metadata inspection, not an exhaustive decode/playability test.
A damaged payload whose usable headers can still be inspected may still return
success. Scanner retry/merge behavior belongs to the application.

## Isolated verification

From the libzune directory:

```sh
bash tests/run-metadata-probe.sh
```

The gate generates temporary MP3, FLAC, M4A, Ogg/Vorbis and Opus files using the
installed FFmpeg, then compiles the actual probe twice. It checks both native
and fallback text/numeric metadata, selected-stream and container precedence,
missing and Unicode-blank tags, malformed values/files, escaped text/filenames,
video-only inspection, zeroed failures and repeated free. Independent boundary
cases cover number overflow and valid/invalid date forms.

Result on Linux/FFmpeg 9: **64/64 native checks and 64/64 fallback checks passed**
under AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer. The rebuilt
C++ header consumer and old-member-offset assertions also passed. The complete
static/shared library compiled in a separate temporary directory. Existing
unrelated warnings remain in track/zmdb/PTP/MTP/MTPZ code; the changed probe
compiled cleanly. No hardware or wire-protocol behavior was exercised or changed.
