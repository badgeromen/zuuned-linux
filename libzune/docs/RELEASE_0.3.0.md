# libzune 0.3.0

Prerelease notes, October 20, 2026, for the `hardening/0.3.0` branch. Every
task in the [hardening spec](HARDENING_0.3.0.md) is implemented and verified
on a Zune HD, a Zune 30 and a Zune 80 (a 120 GB Draco unit); the Scorpius
models (Zune 4/8/16) have not been tested (below). A review by eight agents
then found the defects listed under "Validation fixes", which are fixed.

## Security and correctness fixes

These come from the hardening review of the public 0.2.0 tree (`50999c1`),
which reproduced the shell injection and the ZMDB offset wraparound. Every fix
ships with a sanitizer regression test that fails on the previous code.

- **No shell for external tools (T1.1).** `zune_snap_thumb` and
  `zune_grab_photo_thumb` passed caller paths to `system()` unescaped, so a
  filename could run a command. All tool calls now use `posix_spawn` with exact
  argv; fixed command buffers that could cut a command mid-quote are gone.
- **Tool lookup (T1.2).** The legacy `zune_arm_*` profiles and `zune_retag` no
  longer hard-code Homebrew `ffmpeg` (they always failed on Linux). Tools are
  found through `ZUNE_FFMPEG` / `ZUNE_FFPROBE` / `ZUNE_SIPS`, then `PATH`
  (never the current directory), then on macOS `/opt/homebrew/bin` and
  `/usr/local/bin`.
- **Private temporary files (T1.3).** Prepared outputs are created with
  `mkstemps`, mode 0600, under `$TMPDIR`; predictable names are gone, and
  failed or empty outputs are removed.
- **ZMDB parser (T1.4).** Device offsets, sizes and counts are checked in
  `size_t` by subtraction. Malformed databases fail closed with a specific
  error instead of crashing or returning a partial library. Output for a real
  Zune HD database is identical to 0.2.0.
- **MTP array counts (T1.5).** Storage-ID and reference lists refuse counts
  whose byte size wraps; GetDeviceInfo parsing checks its cursor.
- **Upload retry triage (T1.8).** A refused upload (StoreFull, AccessDenied) is
  tried once and reported with its own code; `zune_autopsy` no longer reports
  the orphan cleanup's OK.
- **MTPZ keys and libgcrypt (T1.6).** Keys load once per process instead of
  on every breach (each reload leaked the previous copy), and `zune_go_dark`
  wipes them before freeing. libgcrypt is initialized only when the host
  application has not done so already.
- **MTPZ out-of-memory handling (T1.7).** A failed allocation anywhere in the
  authentication handshake now fails it cleanly instead of crashing; nothing
  built after the failure is sent to the device.
- **Photo uploads (T1.9).** A file that cannot be converted is sent only if it
  is already a JPEG. Albums are looked up on the device before one is created,
  so a new session no longer duplicates album folders.

Found by the October 19 hardware passes (each with a regression test that
fails on the previous code):

- **Database reads leave the device in sync.** After the ZMDB payload the
  library made one 100 ms read for the device's OK response. When the payload
  ends on a 512-byte boundary the device sends a zero-length packet first, and
  a busy device can answer later; the unread OK was then taken by the next
  command as its own answer, and the device stopped accepting commands. Seen on
  a Zune 80 (a 414,720-byte database); the response is now always consumed.
- **Cancelling a transfer leaves the device usable.** `zune_abort` during an
  upload or download stopped the host but left the device waiting for (or
  sending) the rest of the data; the next command was swallowed and the device
  stopped answering until a USB reset. The transaction is now cancelled with
  the PTP Cancel Request and the device's status is polled until it is ready
  (Linux; seen on a Zune HD).
- **Video profiles** force 8-bit 4:2:0, which x264's baseline profile
  requires; 4:4:4 and 4:2:2 sources previously failed to transcode.
- **Animated GIF/WebP photos** convert their first frame; ffmpeg previously
  refused to write several frames to one JPEG.
- **`zune_decode_filename`** no longer overflows on a season or episode
  number too large for an `int` (`S99999999999E1` read back as season
  1215752191); such numbers are refused.
- **`zune_snap_thumb`** retries from the start of a clip shorter than its
  10-second seek (newer ffmpeg failed; FFmpeg 6.1 reported success without
  writing one), and a failed thumbnail leaves no file behind.

## Validation fixes

Found by the October 20 review (each with a regression test that fails on
the previous code; details in Appendix C of the spec):

- **CI builds.** Both fuzz harnesses had stopped linking after T3.2/T3.5; a
  fuzz smoke run now sits in the ordinary suite loop. `libzune.so` named its
  libraries before its objects, so a linker using `--as-needed` (the
  Debian/Ubuntu default) dropped them and the library failed to load; the
  link now forces `--as-needed` and `-z defs`. `make install PREFIX=...`
  writes `libzune.pc` for that prefix.
- **No duplicate spoken uploads.** A podcast episode or audiobook part whose
  creation the device answered with OK but no handle was retried and could
  exist twice; it is now treated like no answer and not retried.
- **No track loss on a Zune HD.** An album whose track references got no
  answer was deleted, and the HD deletes an album's referenced tracks with it;
  such an album is now kept and reported.
- **Smaller fixes.** `zune_dump_raw` consumes the response like every other
  database read. Tool lookup ignores relative `PATH` entries. A stale file at
  `zune_snap_thumb`'s output can no longer pass as the new thumbnail. A
  successful photo fallback or database scan leaves no error behind.
  `zune_get_folders` fails instead of returning part of the list when the
  device stops answering. Wrappers whose response fails after the data phase
  release the reply (a pre-existing leak). Classic database scans ignore the
  index bytes after the 33-descriptor table, which could reject a whole scan.
  The cancel's status polling stops after 10 s.
- **`mtpz.c`.** With the author's approval its log lines go through
  `zune_wiretap` (the certificate hex dumps at WHISPER) and its two warnings
  are fixed with the same byte values; the crypto code is unchanged.

## Structural cleanup

Internal restructuring from Tier 3 of the spec. Public names and signatures
are unchanged; behavior changes are listed in the next sections.

- **One upload engine (T3.1).** Tracks, videos, photos, podcast episodes and
  audiobook parts share one create, stream, triage, clean-up and retry path.
  Spoken media gain retries after a failed payload is proven removed; an
  unanswered creation is never retried, so a lost response cannot create a
  duplicate.
- **Streamed uploads (T3.2).** Files are sent from disk through a 1 MiB
  buffer instead of being read whole into memory (up to 4 GiB before). A
  400 MB upload raised peak memory by 1 MiB on a Zune HD and 0.9 MiB on a
  Zune 30.
- **Partial failures (T3.3).** An album or playlist whose commit or track
  references fail is deleted and reported as a failure; the error says
  whether the cleanup worked and `zune_autopsy` keeps the original code.
- **Per-device state (T3.4).** The photo album cache belongs to each device
  handle and is freed by `zune_sever`.
- **Error convention (T3.5).** `ZUNE_OK`/`ZUNE_BLOWN`, the error cleared on
  entry, a return-convention table in the API reference, and deprecation
  attributes (see Behavior changes).
- **Atomic flags (T3.6).** Abort and disconnect flags use atomic loads and
  stores.
- **Smaller functions (T3.7).** `zune_breach`, `zune_decode_filename` and
  the ZMDB scan are split into helpers, and duplicated byte, file-name,
  property and preflight code is shared. Equivalence was checked against the
  previous code: 111 file names decode identically, and scans of five real
  device databases are identical field by field.

## New public functions

| Function | Meaning |
|---|---|
| `zune_dossier()` | Running library version, `"0.3.0"` |
| `zune_wiretap(fn, ctx)` | Install a log callback (NULL restores stderr) |
| `zune_set_chatter(level)` | Minimum log level: WHISPER, TALK, SHOUT, SCREAM, SILENT |
| `zune_get_brand_limits(dev, id, &w, &h, &bytes)` | Largest artwork the device declares for an item (Zune 30: 240x240, 1 MiB) |
| `zune_go_dark()` | Wipe and free the MTPZ keys (process teardown; a later breach reloads them) |

`zune.h` also defines `ZUNE_OK` (0) and `ZUNE_BLOWN` (-1) for status results,
and `ZUNE_DEPRECATED(msg)`.


## Behavior changes

- `zune_get_folders` returns -1 with an error when the folder query fails or
  the device stops answering partway through; a device with no folders is
  still 0. `zune_get_tracks` returns NULL with an error on a failed query.
- `zune_breach` fails with an error when the runtime libgcrypt is older than
  the version libzune was built with.
- `zune_forge_album` keeps a new album whose track references got no answer
  (and returns 0 with an error naming it) instead of deleting it.
- `zune_dump_raw` fails on an incomplete read instead of writing part of the
  database.
- `zune_snap_thumb` replaces an existing file at its output path and removes
  it when the grab fails.
- External tools are looked up only in absolute `PATH` entries.
- Video/photo purge and extract failures set the thread-local error.
- Logs: default level TALK; `ZUNE_CHATTER=whisper|talk|shout|scream|silent`
  sets the start level. The device serial, protocol detail and per-item
  progress moved to WHISPER, as did the MTPZ handshake steps and hex dumps;
  `[mtpz]` lines go through `zune_wiretap` like every other tag.
- `ZUNE_PL_FMT` is honored only in `-DZUNE_DEBUG_KNOBS` builds.
- Prepared files are named `zunelinux_<input>_XXXXXX.<ext>`.
- `zune.h` now states that `zune_smuggle_track` does not read file tags
  (unchanged behavior).
- Podcast episode and audiobook part uploads retry (up to three attempts)
  after a failed payload is removed; `ZUNE_PODCAST_FAILED_CLEANED` means every
  episode the call created was removed, and an audiobook part ID is returned
  on failure only when that part could not be removed or failed readback.
- `zune_forge_album` and `zune_forge_playlist` return 0 when the commit or
  the track references fail (the album returned its ID and only warned; the
  playlist ignored a failed commit).
- Upload errors name the caller's file, also when a prepared copy was sent.
- Every call that can fail clears the thread-local error on entry, so
  `zune_get_error` describes the most recent such call; calls that cannot fail
  (`zune_autopsy`, `free_*`, cached getters, `zune_sever`, log settings) leave
  it unchanged. `docs/API_REFERENCE.md` has a table of every function's return
  convention.
- Deprecated, still working: `zune_infiltrate_legacy` (use `zune_infiltrate`
  and `zune_free_scan`) and `zune_smuggle_movie`, `zune_smuggle_episode`,
  `zune_smuggle_clip` and `zune_smuggle_other` (use
  `zune_smuggle_video_named`). GCC and Clang warn at each call.

## Build and packaging

- Only the functions in `zune.h` are exported (`ZUNE_API`,
  `-fvisibility=hidden`).
- Linux: `libzune.so.0.3.0`, SONAME `libzune.so.0`. macOS keeps
  `@rpath/libzune.dylib` and gains current/compatibility versions.
- `libzune.pc` is generated and installed; `Libs.private` lists gcrypt,
  libusb, libav and pthread.
- `make CFLAGS+=...` now extends the Makefile's flags instead of replacing
  them.
- New: libFuzzer harnesses (`tests/fuzz/`), an offline CI workflow, and the
  symbol, wiretap, tool-execution, ZMDB-hostile, MTP-unpack, send-retry,
  photo-upload, honest-failure, brand-limits, MTPZ key and null-check,
  abort-flag, PTP-cancel, forge-partial, upload-engine, upload-stream,
  error-convention, shrink-giants and fuzz-smoke suites. All 48 run offline;
  46 use ASan and UBSan with UBSan errors fatal, the abort-flag suite uses
  TSan, and the symbol gate needs no sanitizer.
- The metadata-probe fixture no longer depends on the shell locale (T2.4).

## Hardware verification

| Task | What to verify | Keel | Scorpius | Draco | Pavo |
|---|---|---|---|---|---|
| T1.6 | breach, sever, breach again; auth succeeds both times | Pass (2026-10-20): three breaches in one process, `zune_go_dark` before the third; keys loaded on the first and third only; OpenSession timed out on the second and third and the breach's reset-and-retry recovered both; authenticated and read storage each time | | Pass (2026-10-20): three breaches in one process, `zune_go_dark` before the third; keys loaded on the first and third only; OpenSession timed out on all three and the breach's reset-and-retry recovered each; authenticated and read storage each time | Pass (2026-10-20): three breaches in one process, `zune_go_dark` before the third; keys loaded on the first and third only; authenticated and read storage each time |
| T1.8 | full device: one track and one photo fail once with StoreFull | Pass (2026-10-20): 239 filler tracks left 0 bytes free; one more 2 MB track and one more photo (57 KB prepared) were each tried once and refused with StoreFull (autopsy 0x200C); fillers removed and the 1,484-object baseline restored; free space came back one 32 KiB cluster below the start (Appendix C) | | Pass (2026-10-20): 162 filler tracks left 0 bytes free; one more 2 MB track and one more photo (57 KB prepared) were each tried once and refused with StoreFull (autopsy 0x200C); fillers removed, free space and the 1,287-object baseline restored | Pass (2026-10-20): 22 filler tracks left 27,291 bytes free; one more 2 MB track and one more photo (57 KB prepared) were each tried once and refused with StoreFull (autopsy 0x200C); fillers removed, free space and the 309-object baseline restored |
| T1.9 | PNG without ffmpeg fails cleanly; two photos, one album folder | Pass (2026-10-20): PNG with ffmpeg unavailable refused with the inventory unchanged and an error naming the file; two photos in a fresh session, one new album folder; removed, baseline kept | | Pass (2026-10-19; rerun 2026-10-20): PNG with ffmpeg unavailable refused with the inventory unchanged and an error naming the file; two photos in a fresh session, one new album folder; removed, baseline kept | Pass (2026-10-19, `37a8c6c`) |
| T3.1 | every media type sends through the one engine; refusals are tried once, failed payloads removed | Pass (2026-10-20): read 26/1 (the failure: three albums store no art, their RepresentativeSampleData is empty), write 18/0, write2 9/0, write3 10/0, playlist 7/0, album pass (no artist objects: 0xB218 answers 0x2016, skipped as before), podcast and audiobook gates, torture 20/20, aborted transfers (autopsy 0x201F) and a 400 MB track; baseline kept | | Pass (2026-10-20): read 27/0, write 18/0, write2 9/0, write3 10/0, playlist 7/0, album pass (no artist objects: 0xB218 answers 0x2016, skipped as before), podcast and audiobook gates, torture 20/20, aborted transfers (autopsy 0x201F) and a 400 MB track; baseline kept | Pass (2026-10-20): read 27/0, write 18/0, write2 9/0, write3 10/0, album pass, podcast and audiobook gates, torture 20/20, an upload aborted mid-transfer (autopsy 0x201F) and a 400 MB track; baseline kept |
| T3.2 | large uploads stream with bounded memory | Pass (2026-10-20): 400 MB track at 13.4 MB/s; peak RSS rose 0.9 MiB | | Pass (2026-10-20): 400 MB track at 13.6 MB/s; peak RSS rose 1.0 MiB | Pass (2026-10-20): 400 MB track at 6.7 MB/s; peak RSS rose 1 MiB |
| T3.3 | an album and a playlist whose references fail are removed and reported | Pass (2026-10-20): both refused with 0xA804, removed and reported; success paths pass; baseline kept | | Pass (2026-10-20): both refused with 0xA804, removed and reported; success paths pass; baseline kept | Pass (2026-10-20): both refused with 0xA804, removed and reported; success paths pass; baseline kept |
| T3.4 | two handles in one process find and reuse one photo album folder | Pass (2026-10-20): second handle read the folders and reused the album; baseline kept | | Pass (2026-10-20): second handle read the folders and reused the album; baseline kept | Pass (2026-10-20): second handle read the folders and reused the album; baseline kept |
| T3.5 | read and write passes unchanged; a failed call's error survives `zune_autopsy` and getters, and the next successful call clears it | Pass (2026-10-20): read, write and write2 as under T3.1, baseline kept; extracting a missing object returned `ZUNE_BLOWN` with its own error (autopsy 0x2009), kept through autopsy and getters and cleared by `zune_refresh_storage` | | Pass (2026-10-20): read, write and write2 as under T3.1, baseline kept; extracting a missing object returned `ZUNE_BLOWN` with its own error (autopsy 0x2009), kept through autopsy and getters and cleared by `zune_refresh_storage` | Pass (2026-10-20): read 27/0, write 18/0, write2 9/0, baseline kept; extracting a missing object returned `ZUNE_BLOWN` with its own error (autopsy 0x2009), kept through autopsy and getters and cleared by `zune_refresh_storage` |
| T3.6 | an abort from another thread stops an upload; the session keeps answering | Pass (2026-10-20): an upload stopped at its first 1 MiB chunk and a download at 20 MiB; they returned 2.1 s and 0.3 s after the abort with autopsy 0x201F, the session kept answering, and the next session's inventory equalled the baseline (Appendix C notes a firmware album handle) | | Pass (2026-10-20): an upload stopped after its first 1 MiB chunk and a download at 23 MiB; they returned 2.2 s and 0.3 s after the abort with autopsy 0x201F, the session kept answering, and the next session's inventory equalled the baseline (the same firmware album handle as on Keel was listed in-session) | Pass (2026-10-20): the upload stopped at 5 MiB and the download at 18 MiB; they returned 1.0 s and 0.3 s after the abort with autopsy 0x201F, the session kept answering, and both kept the baseline |
| T3.7 | breach (three in one process, `zune_go_dark` before the third), read and write passes unchanged after the splits | Pass (2026-10-20): three breaches authenticated (as T1.6); read, write and write2 as under T3.1; baseline kept | | Pass (2026-10-20): three breaches authenticated (as T1.6); read, write and write2 as under T3.1; baseline kept | Pass (2026-10-20): three breaches authenticated, keys loaded on the first and third only; read 27/0, write 18/0, write2 9/0, baseline kept |

Pavo runs (October 19) also covered every public read path, transcoded audio
and video uploads, PNG photo conversion, a 24 MB transfer, playlists, the
podcast and audiobook gates and 20 consecutive sends, each followed by an exact
baseline check. Zuuned connected and inventoried the HD through this library.

The Draco runs (a Zune 80, the same day) repeated those gates and added a
format matrix: 16 audio and 12 video source files converted through both
libzune and Zuuned's own transcoder, and 14 photo formats through libzune
(which Zuuned also uses), each uploaded, read back byte-identical and removed,
with the 1,277-object baseline restored after every session.
Draco firmware has no artist objects (format 0xB218 is refused); album, art,
playlist, podcast and audiobook objects work. Screen playback was not checked.
See Appendix C of the hardening spec.

On October 20 a Zune 30 reported its artwork limits: JPEG only, at most
240x240 and 1 MiB. Zuuned had been sending 1600x1600 audiobook covers that a
Zune 80 did not show; it now fits covers to `zune_get_brand_limits`.

The same day every Tier 3 task and the full-device StoreFull check (T1.8)
passed on the Zune HD, on a Zune 30, whose 128 GB drive took 239 filler
tracks, and on the Zune 80, which reports 119.8 GB and took 162; every session
was followed by an exact check of the device's baseline (309, 1,484 and 1,287
objects). Like the Zune 80, the Zune 30 firmware has no artist objects. Both
hard-drive models often need the OpenSession reset documented before 0.3.0,
and every reset recovered; on the Zune 30, bare reconnects reset no more often
than with 0.2.0. Zuuned built on this library then synced a FLAC album and a
TV episode to the Zune 80 and removed them again, leaving the exact baseline.
No Scorpius model has been run.

After the validation fixes the Zune 80 ran the whole harness pass again, with
a verify that also compares every ZMDB object field by field, and Zuuned
synced and removed three tracks and a podcast episode; every verify matched.
The Zune HD then passed the same rerun and the same app test.
That rerun found a fault in this unit: reads now and then return one byte with
bit 7 set (at byte 120 of a USB packet, about one per 160 MB read). The
stored copies are intact and writes are not affected. A Zune 30 and a Zune
HD on the same port each returned 130 downloads of the same file without
one, so the fault is in that Zune 80, not in libzune or the computer.
Appendix C has the measurements.

## Open items

- Hardware: any Scorpius (Zune 4/8/16) run. T1.8 fills the device; the
  Zune 30's 95 GB took 1 h 52 min and the Zune 80's 65 GB 1 h 13 min. Every
  hardware StoreFull refusal came at creation, so T1.8's data-phase path is
  proven by `tests/send-retry.c` only.
- macOS: the Darwin build (`driverkit_usb.c`, the sips and Homebrew branches,
  the install name) changed but was not built.
- ZMDB scans still cap libraries at 10,000 tracks, 2,000 albums and similar
  (now named `ZMDB_MAX_*` in `zmdb.c`).
- The PTP cancel is not sent after a failed header write in split framing or
  a failed first chunk in combined framing.
- The Zune 80's read flips (Appendix C): a fault in that unit. MTP has no
  checksum, so reads from it can carry one wrong bit; writes are fine.

### Follow-up software review (October 20)

Thumbnail generation now refuses aliased input/output paths without deleting
the source. Compact Classic v2 databases validate their actual descriptor
table before parsing, with truncation checks preserved. Cancelling after an
unanswered upload creation keeps the incomplete-object warning for caller
recovery, even if a later attempt was removed. These fixes have offline
regressions; they do not add physical-device or macOS validation claims.
