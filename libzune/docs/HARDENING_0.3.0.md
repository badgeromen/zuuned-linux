# Operation Clean Getaway: libzune 0.3.0 Hardening Spec

Instructions for an AI coding agent (Claude Code) to bring libzune's older
code paths up to the standard of the 0.2.0 spoken-media work, without losing
the "Zune Rebellion" personality of the public API.

This spec comes from a full review of the public 0.2.0 tree (commit `50999c1`):
a Linux build, all offline test suites, `cppcheck`, `gcc -fanalyzer`, two
line-by-line reviews (parsing layer and API/operations layer) and two
reproduced exploits. Line numbers refer to that commit and will drift; find
the code by function name.

---

## 0. Ground rules

These apply to every task. Read them before touching code.

### 0.1 The naming personality is load-bearing

- **Never rename, remove or change the signature of an existing public
  `zune_*` symbol** unless a task below explicitly says so. The themed verbs
  are part of the product.
- **Every new public function gets a themed name.** Use an existing verb from
  the lexicon below where one fits. If none fits, coin a new verb in the same
  heist / rebellion / field-medic spirit and add it to the lexicon table in
  this file and in `docs/API_REFERENCE.md`.
- Themed names must still be guessable. Every new public function gets a
  one-line plain-English description directly above it in `zune.h`
  ("Set the log callback.", "Return the library version string.").
- Internal (non-exported) helpers may use plain descriptive names. Theme them
  only if it reads naturally; clarity wins inside `src/`.
- Log tags (`[libzune]`, `[ptp]`, `[mtpz]`) stay as they are.

### 0.2 Verb lexicon

Existing verbs (keep using these):

| Verb | Meaning |
|---|---|
| `breach` / `sever` | connect / disconnect |
| `smuggle` | send media to the device |
| `extract` | copy media off the device |
| `forge` | create an object (album, artist, playlist, folder, series) |
| `rewire` | update an object's references |
| `brand` | set artwork |
| `purge` | delete |
| `infiltrate` | read the device database (ZMDB) |
| `autopsy` | explain why the last operation died |
| `unjam` | clear USB stalls |
| `arm` | prepare/transcode a file for the device |
| `snap` / `grab` | make / fetch a thumbnail |
| `probe` | read metadata |
| `abort` | cooperative cancellation |

New verbs introduced by this spec:

| New public API | Meaning |
|---|---|
| `zune_wiretap(fn, ctx)` | install a log callback (replaces raw stderr) |
| `zune_set_chatter(level)` | set the minimum log level |
| `zune_dossier(void)` | return the runtime library version string |
| `zune_go_dark(void)` | process-wide teardown: wipe and free loaded MTPZ keys |

Log level enum, also themed:

```c
typedef enum {
    ZUNE_CHATTER_WHISPER = 0,  /* debug */
    ZUNE_CHATTER_TALK    = 1,  /* info  */
    ZUNE_CHATTER_SHOUT   = 2,  /* warn  */
    ZUNE_CHATTER_SCREAM  = 3,  /* error */
    ZUNE_CHATTER_SILENT  = 4   /* nothing */
} ZuneChatter;
```

### 0.3 No hardware here

The agent cannot reach a Zune. Therefore:

- Do **not** change wire behavior: opcode order, transaction sequences,
  `usleep` pacing, chunk sizes or timeouts. The only exceptions are tasks
  marked **[HW]**, which say exactly what may change.
- Every task marked **[HW]** must add an entry to Appendix A (hardware
  checklist) describing what the author must verify on which models.
- Do not touch the MTPZ crypto math in `src/mtpz.c`. Only the null-checks,
  key-loading and init changes described in T1.6 and T1.7 are allowed there.
- Never add, generate, download or commit MTPZ key material, `mtpz_keys.h`
  or `.mtpz-data`.

### 0.4 Definition of done (every task)

1. `make clean && make` passes with **zero new warnings** under the existing
   `-Wall -Wextra -Wpedantic` flags.
2. Every `tests/run-*.sh` that passed before still passes (all run offline
   under ASan + UBSan). `tests/run-audiobook-gate.sh` needs hardware; skip it.
3. Each bug fix ships with a regression test that fails before the fix and
   passes after, in the existing `tests/` style (plain C, check counters,
   mock data, `run-*.sh` wrapper, sanitizers on). Exception: tasks that are
   pure deletions or docs.
4. Public header changes are mirrored in `docs/API_REFERENCE.md`.
5. One commit per task ID, message prefixed with the ID (`T1.2: ...`).

### 0.5 Branching

Work on a branch named `hardening/0.3.0`. Tiers land in order. Do not start
Tier 3 until the author has completed the Tier 1 entries in Appendix A.

---

## 1. Findings summary

| Area | Rating | Main problems |
|---|---|---|
| Functionality / protocol work | 8/10 | Linux transcode profiles hard-code Homebrew ffmpeg |
| Transport (PTP/USB) | 8/10 | none blocking |
| Spoken-media code | 8/10 | reference standard for the rest |
| Tests | 7/10 | no fuzzing, no CI, one libav-dependent failure |
| Parsing safety | 6/10 | ZMDB offset wraparound (proven), two `4 + n*4` overflows |
| API design | 5/10 | leaked internal symbols, no SONAME, mixed error conventions |
| Older media paths | 5/10 | shell injection (proven), retry triage bug, global state, /tmp hazards |

The 0.2.0 podcast/audiobook code (`podcast.c`, `podcast_state.c`,
`spoken_inventory.c`, `audiobook.c`) is the model to copy: subtraction-first
bounds checks, strict validation, all-or-nothing outputs, verified cleanup.

---

## 2. Tier 1: Security and correctness

### T1.1 Remove the shell from every external tool call

**Problem.** `zune_snap_thumb` (`util.c`) and `zune_grab_photo_thumb`
(`thumbnail.c`) put caller paths into `system()` unescaped. Reproduced: a
filename containing `'; touch pwned; '` executes the command. A plain title
like `Don't Stop.mp4` also breaks the call. The escaped calls in `photo.c`
and `transcode.c` write into fixed `cmd[2048]` buffers without checking
`snprintf` truncation, so a long path can cut the command mid-quote.

**Fix.**
- Add an internal helper (in `util.c`, declared in `zune_internal.h`):
  `int zune_run_tool(const char *tool, const char *const argv[], int capture_fd);`
  It uses `fork` + `execvp` (or `posix_spawnp`), no shell, `waitpid`,
  returns the exit status, and sends child stdout/stderr to `/dev/null`
  unless a capture fd is given. `zune_probe` already does fork/exec without a
  shell (`util.c`, non-libav path); reuse that pattern.
- Replace **every** `system()` and `popen()` in `src/` with `zune_run_tool`.
  After this task, `grep -nE '\b(system|popen)\s*\(' src/*.c` returns nothing
  except the commented-out block deleted in T2.6.
- Convert each command to an argv array. Shell quoting disappears, but
  ffmpeg filter quoting stays: in `photo.c` the `-vf` argument becomes the
  single argv element
  `scale='min(480,iw)':'min(480,ih)':force_original_aspect_ratio=decrease`.
- Delete `shell_escape` and its duplicate once nothing uses them.

**Tests.** `tests/run-tool-exec.sh`: put a fake `ffmpeg` script first on
`PATH` that records its argv to a file, then call `zune_snap_thumb` with
paths containing `'`, `"`, `$()`, backticks, `;`, spaces and a 3000-byte
name. Assert no injected side effect and that argv arrived intact.

### T1.2 Tool lookup without hard-coded Homebrew paths

**Problem.** `transcode.c` calls `/opt/homebrew/bin/ffmpeg` in every
profile, so `zune_arm_*` fails on Linux. `util.c` tries
`/opt/homebrew/bin/ffprobe` first. Other files use bare `ffmpeg`.

**Fix.** One internal resolver, `const char *zune_tool_path(const char *name)`:
1. Environment override: `ZUNE_FFMPEG`, `ZUNE_FFPROBE` (and `ZUNE_SIPS` on macOS).
2. `PATH` search.
3. On macOS only, fall back to `/opt/homebrew/bin` then `/usr/local/bin`
   (GUI apps launched from Finder do not inherit the shell `PATH`).

All tool calls go through it. If a tool is missing, set the thread-local
error to name the tool and the environment variable that overrides it.

**Tests.** Extend `run-tool-exec.sh`: env override wins, `PATH` lookup works,
missing tool yields a clear error.

### T1.3 Safe temporary files

**Problem.** Predictable names: `transcode.c` (basename + pid), `photo.c`
(pid + second), `thumbnail.c` (`/tmp/zunelinux-photo-<id>`). ffmpeg runs with
`-y`, so a pre-planted symlink gets overwritten. Failed outputs are left on
disk (`transcode.c` failure paths, `photo.c` prepare failure).

**Fix.**
- Internal helper `char *zune_mktemp(const char *suffix)` using `mkstemps`
  under `$TMPDIR` (default `/tmp`), file mode 0600, returning a malloc'd path.
  ffmpeg's `-y` then overwrites a file the library created itself.
- Every temp file is unlinked on every failure path. Successful outputs keep
  their current ownership contract (caller frees and unlinks, as documented).

**Tests.** Assert temp names are unique across two calls in the same second
and that a forced tool failure leaves no file behind.

### T1.4 ZMDB parser: overflow-safe offsets

**Problem.** `zmdb.c` reads 32-bit offsets and sizes from the device and
checks them as `off + k > data_len` in `uint32_t`, which wraps. Proven: a
crafted blob with descriptor-0 `data_offset = 0xFFFFFFF8` passes the check
and segfaults in `rd32` under ASan. Affected spots include the index loop in
`zmdb_parse_full` (~626 and ~663), ~553, ~795, and the macro at ~839.

**Fix.**
- Add one helper and use it for **every** bounds check in `zmdb.c`:
  ```c
  /* 1 if [off, off+need) lies inside a buffer of length len. */
  static inline int zmdb_span_ok(size_t len, size_t off, size_t need)
  { return off <= len && need <= len - off; }
  ```
- Compute offsets in `size_t`, never in `uint32_t` arithmetic.
- Validate `entry_count` against `(data_len - data_offset) / 8` **before**
  allocating, and stop casting the count to `int` (~623, ~792).
- Make `rd16`/`rd32` cast each byte to `uint32_t` before shifting
  (`buf[3] << 24` currently shifts into the sign bit, which UBSan flags).
- Match `podcast_state.c`: on any bounds failure, fail closed (return error,
  free partial results). Do not silently truncate.

**Tests.** `tests/zmdb-hostile.c` + `run-zmdb-hostile.sh`: wrapped
`data_offset`, huge `entry_count`, entry pointing one byte past the end,
zero-length blob, truncated header. All must return an error with no ASan
or UBSan report. Commit the reviewer's proof harness input as one case.

### T1.5 MTP array unpackers: `4 + n*4` overflow

**Problem.** `mtp_get_storage_ids` (`mtp.c` ~415) and
`mtp_get_object_references` (~1177) check `len < 4 + n * 4` with a
device-supplied `n`. For `n = 0x40000000` the check wraps to `len < 4`,
`malloc(n * 4)` wraps to `malloc(0)`, and the loop writes out of bounds.

**Fix.** Copy the correct pattern already in `mtp.c` (~550):
`if (len < 4 || n > (len - 4) / 4) fail;`. Then grep all of `src/` for
`\* *[0-9]+` inside length comparisons and allocation sizes and fix any
other instance.

**Tests.** Feed each unpacker a crafted buffer with a wrapping count; expect
failure, no sanitizer report.

### T1.6 MTPZ key loading and libgcrypt init  **[HW]**

**Problem.**
- `zune_breach` reloads `~/.mtpz-data` on every call (`device.c` ~60).
  `fgets_strip` overwrites the static key pointers in `mtpz.c`, so every
  breach after the first leaks the previous keys. `mtpz_free_keys` is never
  called.
- `zune_breach` calls `GCRYCTL_INITIALIZATION_FINISHED` on every connect
  (`device.c` ~56). This takes over the host application's libgcrypt setup.

**Fix.**
- Load keys once per process behind `pthread_once`. Keep the existing
  precedence: `~/.mtpz-data` wins over compiled defaults.
- Add `void zune_go_dark(void);` which wipes the key buffers (explicit
  `memset` through a volatile pointer, or `gcry_free` if allocated with
  gcrypt) and frees them. Safe to call when nothing is loaded. Document that
  no device may be open when it is called.
- libgcrypt: initialize only if the host has not:
  ```c
  if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) {
      if (!gcry_check_version(GCRYPT_VERSION)) { set error; fail; }
      gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
  }
  ```
- Link with `-pthread`.

**Tests.** Mock key file, call the loader twice: one allocation under ASan
leak detection, `zune_go_dark` frees everything.

**Hardware.** Breach, sever, breach again on Keel and Pavo; authentication
still succeeds both times.

### T1.7 MTPZ null-checks

**Problem.** `gcc -fanalyzer` reports about 20 unchecked `malloc` results
in `mtpz.c` (handshake buffers, hash state, `fgets_strip`).

**Fix.** Add NULL checks that fail the handshake cleanly. Do not alter
computations, buffer sizes or message layouts. Keep the LGPL header notice
at the top of the file untouched.

### T1.8 Send-retry triage reads the wrong response code  **[HW]**

**Problem.** In `send_track_internal` (`track.c` ~318) and the photo send
loop (`photo.c` ~450), a failed send deletes the orphan object first and
then calls `zune_wound_is_mortal(dev->ptp.last_response)`. `DeleteObject`
has already overwritten `last_response`, usually with OK. So StoreFull and
AccessDenied are retried three times, and the final error message reports
the wrong code. `video.c` already fixes this and its comment describes the
bug.

**Fix.** Save `failure_response = dev->ptp.last_response` before cleanup,
triage on it, report it in `zune_set_error`, and restore it so
`zune_autopsy` returns the real cause after the call. (T3.1 later merges all
three loops; fix the bug now so it lands even if Tier 3 slips.)

**Tests.** Mock transport: send fails with 0x200C, delete succeeds. Assert
one attempt and `zune_autopsy` returns 0x200C.

**Hardware.** Fill a device, attempt one more track and one more photo.
Expect a single attempt and a StoreFull error.

### T1.9 Photo upload correctness  **[HW]**

**Problem.**
- `zune_smuggle_photo` falls back to the original file when conversion
  fails (`photo.c` ~368) but still declares JPEG and a `.jpg` name.
- Album lookup only consults the process-global `folder_cache`
  (~382-404). Without a prior `zune_get_photos`, it creates duplicate album
  folders.

**Fix.**
- If conversion fails, send the original only when it already is a JPEG
  (check magic bytes `FF D8 FF`). Otherwise fail with a clear error.
- On a cache miss, query the device for the folder before creating one.

**Hardware.** Upload a PNG with ffmpeg removed (must fail cleanly). Upload two
photos to the same album in a fresh session (one folder only).

---

## 3. Tier 2: Library hygiene

### T2.1 Symbol visibility, SONAME, pkg-config, runtime version

**Problem.** 151 exported text symbols, 46 internal (`mtp_*`, `ptp_*`,
`mtpz_*` including `mtpz_load_keys`, and internal `zune_*` such as
`zune_set_error`, `zune_usb_find_device`, `zune_zmdb_read_payload`). No
SONAME, no pkg-config file, version only as macros.

**Fix.**
- Add `ZUNE_API` to `zune.h` (`__attribute__((visibility("default")))` on
  GCC/Clang) and put it on every public declaration. Build with
  `-fvisibility=hidden`.
- Tests and `tools/zunetool.c` compile sources directly or link the static
  archive, so they keep internal access. Confirm all `run-*.sh` still pass.
- Linux: `libzune.so.0.3.0`, SONAME `libzune.so.0`, symlinks `libzune.so.0`
  and `libzune.so`. macOS: **keep** `-install_name @rpath/libzune.dylib`
  (the Zuuned app links against it); add `-current_version` and
  `-compatibility_version` only.
- `make install` writes `libzune.pc` with `Libs.private` listing gcrypt,
  libusb and libav.
- Add `const char *zune_dossier(void);` returning `"0.3.0"`, generated from
  the existing version macros.
- Bump the version macros to 0.3.0.

**Acceptance.** `nm -D --defined-only libzune.so | grep ' T '` lists only
`zune_*` names, all of them declared in `zune.h`. Add this as
`tests/run-symbols.sh`.

### T2.2 Logging: `zune_wiretap`

**Problem.** 339 direct `stderr` writes, no way for an app to capture or
silence them. One logs full ffmpeg command lines (`transcode.c` ~284), one
logs the device serial at connect (`device.c` ~239).

**Fix.**
- Public API:
  ```c
  typedef void (*ZuneWiretapFn)(ZuneChatter level, const char *tag,
                                const char *message, void *ctx);
  /* Set the log callback. NULL restores the default stderr sink. */
  ZUNE_API void zune_wiretap(ZuneWiretapFn fn, void *ctx);
  /* Set the minimum level that reaches the sink. Default: TALK. */
  ZUNE_API void zune_set_chatter(ZuneChatter level);
  ```
- Internal macro `ZLOG(level, tag, fmt, ...)`. The default sink prints the
  exact current format (`[libzune] ...`), so apps that set nothing see the
  same output.
- `ZUNE_CHATTER` environment variable (`whisper|talk|shout|scream|silent`)
  sets the initial level.
- Convert every `fprintf(stderr, ...)` in `src/`. Choose levels by meaning:
  per-chunk/progress noise is WHISPER, lifecycle is TALK, retries are SHOUT,
  failures are SCREAM. The serial number and full tool command lines move to
  WHISPER.
- The callback pointer and level are process-global; document that they
  should be set before any device is opened.

**Tests.** Install a tap, trigger a known failure, assert level, tag and
text; assert `SILENT` suppresses everything.

### T2.3 Fuzz harnesses and CI

- `tests/fuzz/fuzz_zmdb.c` and `tests/fuzz/fuzz_mtp_unpack.c` (libFuzzer,
  `clang -fsanitize=fuzzer,address,undefined`), with seed corpora from the
  existing fixtures and the T1.4/T1.5 crash inputs.
- `.github/workflows/ci.yml` on Ubuntu: install deps, `make`, warnings as
  errors (`make CFLAGS+=-Werror`), every offline `run-*.sh`,
  `run-symbols.sh`, and 60 seconds of each fuzzer.

### T2.4 Fix the failing metadata-probe check

`tests/run-metadata-probe.sh` fails "ASCII and Unicode whitespace-only tags
are missing" with FFmpeg 6.1 (Ubuntu 24.04). Find out whether the probe or
the test depends on libav version behavior and fix the code if the probe is
wrong. Do not weaken the assertion to make it pass.

### T2.5 Report failures honestly (non-breaking)

- `zune_get_folders` returns 0 when the handle query fails (`device.c`
  ~467). Return -1 and set the error.
- `zune_get_photo_albums` does not NULL-check `album_count` (`photo.c`
  ~239).
- `zune_get_tracks` ignores handle-query failures (`track.c` ~83).
- Video and photo purge/extract and photo send failures only log
  (`video.c` ~616-635, `photo.c` ~469-497). Set the thread-local error too.
- `ZUNE_PL_FMT` is read from the environment in shipping code
  (`playlist.c` ~146). Compile it in only under `#ifdef ZUNE_DEBUG_KNOBS`.
- `zune.h` says `zune_smuggle_track` extracts tags. It sends the filename
  and "Unknown Artist" (`track.c` ~373). Correct the header and docs; do not
  change behavior.

### T2.6 Dead code and housekeeping

- Delete the commented-out `killall` / `pkill` block in `device.c`.
- Delete the `#if 0` block in `finalize.c`.
- `device.c` ~207: if/else branches are identical. Collapse them.
- Remove `.DS_Store` from git; add it to `.gitignore`.
- Fix the unused-variable/function/parameter warnings and the `char`
  overflow at `mtpz.c` ~1548 (use `unsigned char` or an explicit cast; same
  byte value).
- **Do not** remove the second `GetDeviceInfo` in `zune_breach`. The first
  runs before MTPZ on purpose. Leave a comment explaining both calls.

### T2.7 Attribution

`CREDITS.md` says no code was copied, but `src/mtpz.c` states it is ported
from libmtp's `mtpz.c` by Sajid Anwar (LGPL-2.1-or-later). Add a libmtp /
Sajid Anwar section to `CREDITS.md`, correct the "no code was copied"
sentence to name this exception, and leave the original notice in
`mtpz.c` intact.

---

## 4. Tier 3: Structural cleanup

Start only after the author completes the Tier 1 entries in Appendix A.
Every Tier 3 task is **[HW]**.

### T3.1 One upload engine

Merge the three send-and-retry loops (`track.c` ~283-330, `video.c`
~218-251, `photo.c` ~432-460) into one internal function, e.g.
`int zune_smuggle_core(ZuneDevice *dev, const ObjectInfo *info,
const char *path, uint32_t *out_handle)`. It owns: abort checks, saving
`failure_response` before cleanup, mortal triage, `zune_unjam` + pacing
between attempts (keep current delays), orphan cleanup, error reporting.
Public functions keep their names and signatures and become thin wrappers.

### T3.2 Stream uploads

`mtp.c` (~883-901) reads whole files into memory for track and video sends
(up to 4 GiB). Send in chunks from a file descriptor; the data-phase
container length is known up front from the file size. Keep the transport's
existing chunk size and ZLP behavior. Podcast and audiobook currently use
`mmap`; move them onto the same path so all media shares one engine (and
gains the retry they currently lack).

### T3.3 Partial failures are failures

- `zune_forge_playlist` ignores the commit result (`playlist.c` ~193).
- `zune_forge_album` only warns when setting references fails (`album.c`
  ~123-129).

Follow the podcast pattern: on failure, purge the object just created,
return failure, and set the error. If cleanup also fails, say so in the
error text and keep `zune_autopsy` pointing at the original failure.

### T3.4 Per-device state

Move `folder_cache` (`photo.c` ~25) into `struct ZuneDevice` and free it in
`zune_sever`. Any other process-global mutable state found along the way
moves too (except the T1.6 keys and T2.2 log settings, which are
process-wide by design).

### T3.5 Error convention for 0.3.0

Without renaming anything:
- Add `#define ZUNE_OK 0` and `#define ZUNE_BLOWN (-1)` and use them for
  every function that returns int status.
- Every public call clears the thread-local error on entry, so
  `zune_get_error` always describes the most recent call.
- Document each function's return convention in one table in
  `docs/API_REFERENCE.md` (status, handle-or-0, pointer-or-NULL, count-or-error).
- Mark superseded functions (`zune_infiltrate_legacy`, the four legacy video
  wrappers) with `__attribute__((deprecated("use zune_...")))`.

### T3.6 Atomics for abort and disconnect flags

Replace `volatile int` `cancel_requested` and `disconnecting` with
`__atomic_load_n` / `__atomic_store_n` through small internal macros
(works on GCC and Clang without dropping C99). Keep the documented
`zune_sever` teardown contract.

### T3.7 Shrink the giants

Split without behavior change: `zune_breach` (266 lines) into open / session /
auth / identify / storage helpers; `zune_decode_filename` (241 lines, seven
copy-pasted pattern blocks) into a table of patterns; `zmdb_parse_full`
(155 lines). Consolidate duplicated helpers: `filename_no_ext`,
`prop_supported`, the four string-property readers, the descriptor preflight
(`podcast.c` ~123 vs `audiobook.c` ~276) and inline little-endian decodes.

---

## 5. Release

- `docs/RELEASE_0.3.0.md`: security fixes (credit the review), new APIs with
  their themed names, behavior changes, the SONAME, and the hardware
  verification status copied from Appendix A.
- Update `docs/API_REFERENCE.md`, `README.md` (build section: tool lookup and
  env overrides) and `docs/TOC.md`.
- Add a pointer to this spec in `CLAUDE.md` under a new "Active hardening
  workstream" heading. Keep the existing spoken-media notes.

---

## Appendix A: Hardware checklist

The agent appends one row per **[HW]** task. The author fills in results.

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

## Appendix B: Out of scope

- Windows backend.
- Changing the GPL-3.0-or-later license.
- Any change to MTPZ crypto math or key handling beyond T1.6/T1.7.
- The `ZuneUSBDriver` DriverKit extension (not in this repo).
- Zuuned application code.

---

## Appendix C: Implementation log

Kept by the implementing agent; the spec above is unchanged.

**2026-10-19 baseline.** Branch `hardening/0.3.0` starts at `850dcca` (the
HD artist-artwork API commit) in this repository and at the matching
Zuuned application commit `0cb1b6e`. Before any change, `make clean && make`
produced the five warnings listed in T2.6, and every offline `tests/run-*.sh`
passed (the two HD CLI suites need `make zunetool` first). The Linux
`run-metadata-probe.sh` passes on this host's FFmpeg 9.0.1; T2.4 concerns
FFmpeg 6.1.

**Hardware access.** The user attached a Zune HD (Pavo) on 2026-10-19, so the
Pavo column of Appendix A can carry agent-run evidence. Keel, Scorpius and
Draco columns stay author gates. The retained `HDArtworkProbe-BillieEilish`
and Disturbed test objects on that HD are not hardening test material and
must not be modified or deleted.

**T1.2.** `zune_tool_path` returns a thread-local buffer (valid until that
thread's next lookup) because preparation runs on several app threads at once.
`zune_run_tool` resolves bare names itself; explicit paths pass through.

**T1.3.** The internal helper is `zune_mktemp(label, suffix)`, not
`zune_mktemp(suffix)`. The label keeps the old readable stem
(`zunelinux_<input>_XXXXXX.mp3`): callers that send a prepared file directly get
its basename as the device ObjectFileName, so a bare random name would be a
visible regression. The label is cut at 96 bytes on a UTF-8 boundary. A tool
that exits 0 without writing anything now counts as a failure.

**T1.4.** Both parsers share one loader that validates the header, the full
96-slot descriptor table and every index record (offset, size and sign bit)
before any media is read, matching `podcast_state.c`, which already passes on
the user's Keel, Draco and Pavo databases. Each descriptor the parser walks
must be a 4-byte-or-larger atom array inside the payload. Discovery still
skips unreadable descriptors, but the slots that hold media on both families
(1, 3, 4, 11, 12, 15, 16) must be readable when populated, so a corrupt music
descriptor cannot yield an empty library. Classic slots past ~32 hold
secondary data and are deliberately not required to be atom arrays. Flagged
(sign-bit) records and unindexed atoms are still skipped, as before. Index
lookups are a binary search over a stable sort, preserving first-match
results. The old and new parsers produce identical output for the real HD
database `build/hd-art-evidence/hd-bound-reconnected.zmdb` (local, private).
Existing per-type caps (10,000 tracks, 2,000 albums, ...) are unchanged and
still truncate very large libraries; that is a separate decision.

**T1.5.** Beyond the two named unpackers, the sweep fixed the GetDeviceInfo
array skip in `zune_breach` (now `parse_device_info`, which also collapses the
identical if/else branches listed in T2.6) and the outbound buffer sizes in
`mtp_set_object_references` / `mtp_set_object_prop_value_ucs2`.

**2026-10-20: hardware gates delegated.** The user asked the agent to finish
the spec, including Tier 3, and to run every hardware check itself on whichever
Zune is attached instead of waiting for author results. All Appendix A columns
may now carry agent-run evidence, each with its date.

**T1.6.** Keys load at most once per process under a mutex-guarded flag rather
than `pthread_once`, because `zune_go_dark` must re-arm loading: a later breach
reads the keys again. A failed load is remembered too (no retry per breach).
Every key buffer records its allocation size and is overwritten through a
volatile pointer before `free`, as are the temporary hex copies read from
`~/.mtpz-data`; that file is read unbuffered so no stdio buffer keeps the key
text. libgcrypt setup moved to `mtpz_init_gcrypt` (`pthread_once`): it checks
`GCRYCTL_INITIALIZATION_FINISHED_P`, requires at least the build's
`GCRYPT_VERSION`, and otherwise leaves a host's own setup alone;
`zune_breach` fails with an error when the runtime is too old.
`tests/run-mtpz-keys.sh` (11 checks, mock key file with fake values in a
temporary HOME) fails 5 of its 8 key checks and reports 6 ASan leaks against the
previous `mtpz.c`. On Pavo three breaches in one process all authenticated.

**T1.7.** All 28 handshake allocations in `mtpz.c` go through `mtpz_alloc`,
which records a failure in a thread-local flag; key-file helpers check
`fgets_strip`, `hex_to_bytes` and the embedded `strdup` copies directly.
On a failure each helper frees what it holds and returns before computing,
callers stop before using a NULL result, and `mtpz_handshake` checks the flag
before every message it sends. Where a helper's output is parsed (the
decrypted response block), the flag is checked right after the call so
undecrypted lengths are never followed. No computation, buffer size or
message layout changed: GCC's analyzer null warnings are gone; two
pre-existing buffer-size notes on casts remain, as does a reported leak of
`v5` on the path where it is NULL (a false positive). The ported hash/AES code
also reads 32-bit words at odd offsets and shifts into the sign bit; that is
crypto math and stays as it is. `tests/run-mtpz-nullcheck.sh` builds the file
with a counting allocator and drives the real handshake against a fake device
whose response uses a throwaway RSA key generated in memory: each of the 70
allocations failing alone fails the handshake with no crash, leak or later
send, and the next handshake succeeds. The previous `mtpz.c` crashes on the
first injected failure. Pavo still authenticated three times in one process.

**T3.6.** The two flags are plain `int`s accessed only through
`ZUNE_FLAG_GET`/`ZUNE_FLAG_SET` (`__atomic_load_n`/`__atomic_store_n`, in
`ptp.h` so the PTP layer's borrowed `abort_flag` uses them too); 66 direct
accesses in `src/` and `tools/` were converted. ThreadSanitizer does not
instrument `volatile` accesses (GCC or Clang), so `tests/run-abort-flags.sh`
pairs a source check (fails on the previous tree) with a ThreadSanitizer run
of `zune_abort` against a polling thread. On Pavo an abort set on the main
thread stopped an upload on a worker thread within one chunk; that run also
exposed the data-phase cancellation bug fixed separately below.

**Data-phase cancellation (found by the T3.6 hardware gate).** An abort
stopped sending (or reading) mid data phase but left the device expecting the
rest of the declared length. On Pavo the orphan cleanup's DeleteObject was
swallowed as payload, the session stopped answering, and only the next
breach's USB reset recovered it (the HD then dropped the partial object). The
PTP layer now cancels the transaction at each of its three abort points:
Cancel Request (class request 0x64, code 0x4001 and the transaction ID), Get
Device Status (0x67) until OK with listed stalled endpoints cleared, then
queued IN data is drained; the aborted call records TransactionCancelled
(0x201F). The USB backend gained an optional `class_request` entry (libusb on
Linux; the DriverKit backend leaves it unset, keeping the old behavior there).
On Pavo the HD reported busy four times (about 0.4 s), then OK, and had already
dropped the partly sent object. An aborted 100 MB upload now returns 0.9 s
after the abort, where it had hung for 12 s, and an aborted download of the
same track leaves the session answering; both keep the baseline. Firmware
side objects created for the fully uploaded track vanish by the next session.
`tests/run-ptp-cancel.sh`: 10 checks on a scripted backend, 9 of which fail on
the previous `ptp.c`.

**T3.4.** The photo album folder cache is now `ZuneDevice.folder_cache`,
freed by `zune_sever` (`zune_free_folder_cache`); a survey of `src/` found no
other process-global mutable state besides the T1.6 keys, the libgcrypt setup
flag and the T2.2 log settings (the AES tables are constant data without
`const`, inside the crypto file). `zune_get_photo_albums` now reads names from
its device's cache and tolerates a NULL device. `tests/run-photo-upload.sh`
gains a second device in the same process with no matching album: it must
look up and create its own folder (the previous code reused the first
device's folder ID). On Pavo one process breached, sent a photo to a new
album, severed, breached again and sent a second photo: the new handle read
the folders from the device and reused the one album; cleanup restored the
309-object baseline.

**T3.3.** `zune_forge_failed` (album.c) follows the podcast cleanup: it saves
the failing response, deletes the new object, restores the response for
`zune_autopsy` and reports in the error whether the delete worked. Album and
playlist forges use it when the zero-byte commit fails (the playlist ignored
that result before) and when SetObjectReferences fails (the album only
warned and the playlist returned its ID). `tests/run-forge-partial.sh`: 8
checks, 5 of which fail on the previous code. On Pavo a playlist and an album
referencing a nonexistent track were refused with 0xA804
(Invalid_ObjectReference, now named by `zune_autopsy_name`), removed and
reported as failed; the success paths still pass. In the same session the
inventory showed one extra firmware object, gone in the next session, which
matched the baseline exactly. Pavo note: deleting an album object also
deleted its referenced test track (the track's own delete then returned
0x2009).

**T3.2.** The PTP send data phase reads its payload from a source: the
caller's buffer, or a file read with `pread` into one CONTEXT_BLOCK_SIZE
(1 MiB) staging buffer per chunk. `ptp_transaction_file` and
`mtp_send_object_from_fd` stream an open file; `mtp_send_object_from_file`
opens one, refuses it if its size differs from the size declared at creation,
and streams it. Chunk sizes, packet alignment and the payload-length ZLP rule
are unchanged. A data phase the host cannot finish (read error, failed write,
abort) and a receive whose read fails now cancel the transaction, as an abort
does, so the next command is not taken as payload. Tests whose fake device
answers at the `ptp_transaction` level gain `tests/ptp_file_adapter.c`.
`tests/run-upload-stream.sh` sends a 5 MiB file through the real MTP/PTP code
to a bulk-level fake device in split and combined framing: every byte arrives
and the largest allocation is the 1 MiB staging buffer (the previous code
allocated the whole file). On Pavo a 400 MB track uploaded at 6.7 MB/s while
the process's peak RSS rose by 1 MiB (26,776 to 27,800 KiB). Podcasts and
audiobooks move onto this path with the upload engine (T3.1).

**T3.1.** `zune_smuggle_core` (src/upload.c) takes a `zune_upload_t`: the
object is created with SendObjectInfo (tracks, videos, photos) or
SendObjectPropList (podcast episodes, audiobook parts), and the file is
streamed from a descriptor opened once and checked against the declared size.
It owns the attempt loop: abort and disconnect checks, the failed response
saved before cleanup (`zune_autopsy` names it, including after an abort),
mortal triage, removal of a partial object (InvalidObjectHandle counts as
already gone), unjam and the existing 500 ms pacing, and the error text. It
returns SENT, REFUSED (nothing created), REMOVED (every object it created
was removed), UNCERTAIN (a creation went unanswered, so an object may exist)
or ORPHANED (an object could not be removed; its handle is returned and no
further attempt is made). Track, video and photo sends are thin wrappers, as
the spec asks; their public names, signatures and success results are
unchanged. Podcast episodes and audiobook parts now use the engine too, in
strict mode: an unanswered creation is never retried, so a lost response
cannot create a duplicate, and a retry happens only after the partial object
is proven removed. This keeps the spoken no-blind-retry rule (CLAUDE.md)
while adding the retry the spec asks for. REMOVED maps to
ZUNE_PODCAST_FAILED_CLEANED; ORPHANED returns the ID for inspection, and
readback failures after a complete upload still keep the ID and never
delete. `tests/run-upload-engine.sh`: 15 checks; `tests/run-audiobook-protocol.sh`
now expects a payload that keeps failing to leave nothing (3 creates, 3
removals, no ID) and a single failure to be removed and retried. On Pavo
through the engine: the read pass (27/0), FLAC-to-MP3 track, transcoded video,
photo, poster, PNG album photo, 24 MB track, WMV TV episode and album passes
(the album pass's own track delete reports 0x2009 because the HD deletes an
album's tracks with it), `zunetool podcast-test` and `audiobook-test` (exact
bytes, metadata and cover; cleanup restored the inventory), torture 20/20, an
upload aborted at 1.5 s (cancelled, autopsy 0x201F, baseline kept) and a
400 MB track; the inventory then equalled the 309-object baseline with
identical ZMDB counts.
Follow-up (found by the T1.8 full-device run): the engine named the file it
opened, so a refused photo's error showed the prepared temporary copy
(`/tmp/zunelinux_photo_*.jpg`) instead of the caller's file, as before T3.1.
`zune_upload_t.source` now names the caller's file in errors (the photo
path sets it); `tests/run-photo-upload.sh` refuses a converted PNG with
StoreFull and checks one attempt, the code and the original name (fails on
the previous engine).

**T3.5.** `ZUNE_OK` (0) and `ZUNE_BLOWN` (-1) are defined in `zune.h`, and the
public functions that return a status use them (literal returns, the four
`? 0 : -1` results and the status variables of `zune_set_track_state` and
`zune_probe`; values unchanged). Deviation: the spec asks every public call to
clear the error on entry. The 78 calls that can fail do; the 36 that cannot
(`zune_autopsy`, `zune_autopsy_name`, `zune_get_error`, the `free_*` releasers,
the cached getters, `zune_abort`/`zune_clear_abort`/`zune_is_aborted`,
`zune_sever`, `zune_go_dark` and the log settings) leave it alone. The library
itself calls `zune_autopsy_name` after setting an error in several failure
paths, and callers read `zune_autopsy` or release partial results before
reading the message; clearing in those calls would erase the message being
reported. The 13 places where library code calls another clearing public
function were checked: none relies on an error set before that call. Every
read of `zune_get_error` in Zuuned directly follows the failed call. The
return-convention table is generated by `tools/update-api-reference.mjs` from a
per-function map that must cover every declaration and fit its declared return
type; it also says which calls clear the error. `ZUNE_DEPRECATED(msg)` marks
`zune_infiltrate_legacy` ("use zune_infiltrate and zune_free_scan") and the four
legacy video helpers ("use zune_smuggle_video_named"); nothing in the library,
zunetool, the examples or Zuuned calls them, and the two tests that exercise
them on purpose suppress the warning locally. Test fakes that stub
`zune_set_error` gain a `zune_clear_error` stub. `tests/run-error-convention.sh`:
the source check (each definition starts with `zune_clear_error()` exactly when
the table says it clears, and no status function returns a bare 0/-1), 116
sanitizer checks that call every public function except `zune_breach` with
NULL arguments after planting an earlier error (45 fail on the previous code),
and the deprecation warnings in C and C++. Hardware: see Appendix A.

**T3.7.** Splits, with the code moved verbatim: `zune_breach` (now 37 lines)
calls `breach_open` (allocation and the USB retry loop), `breach_session`
(PTP session with the Keel reset-and-retry), `breach_auth` (the first
GetDeviceInfo, then MTPZ), `breach_identify` (the parsed GetDeviceInfo,
friendly name, family and battery) and `breach_storage`, in the original
order. `zune_decode_filename` (61 lines) runs the SxxExx scanner
(`decode_season_episode`), then a six-row table of the episode-only patterns
(regex, case flag, capture group, series source) in the original order with
their documentation; a pattern that matched with episode 0 no longer leaks
its series string when a later one matches. `zmdb_parse_full` (20 lines)
calls `zmdb_full_preflight`, `zmdb_alloc_library`, `zmdb_parse_media` and
`zmdb_log_playlists`; both scans share named capacities (`ZMDB_MAX_*`,
values unchanged). Consolidated: `src/bytes.h` (`zune_le16/32/64`,
`zune_put_le16/32/64`) replaces the copies in `ptp.c`, `mtp.c`, `zmdb.c`,
`zmdb_io.c` and `podcast_state.c` and the inline decodes in the property
readers (several shifted a promoted `int` by 24, undefined for a top byte
above 0x7F; two host-order `memcpy` reads in `track.c` and one in `device.c`
are now little-endian on every host). `zune_filename_no_ext` replaces the two
`filename_no_ext` copies with `track.c`'s rule (a leading dot is part of the
name), which changes only a prepared temporary file's label for a source
named like `.name`. `zune_prop_listed` replaces `prop_supported` and
`pl_prop_supported`. `mtp_read_string_prop` replaces the string-property
reads in `track.c`, `video.c`, `album.c` and `playlist.c` (same decoding;
some copies leaked an empty reply). `mtp_prop_desc_writable` is the
descriptor check shared by the podcast and audiobook preflights, which keep
their own control flow and error text. The spoken readers stay separate on
purpose: they validate counted strings strictly. Evidence: the original
decoder's results for 115 names (every pattern, the guards, zero-episode
and overflow edges, tabs, Unicode) are the golden table in
`tests/run-shrink-giants.sh`, which the new code reproduces exactly (111
committed; four overflow names are excluded, see the next entry); the
original leaked 31 bytes over that corpus and the new code none. Local
only (device data, not committed): field-by-field dumps of `zune_infiltrate`
over five saved databases (Zune HD and Zune 80, up to 708 tracks and 181
videos) are identical before and after. `tests/mtp-helpers.c` covers the two
MTP helpers against a fake MTP layer. Hardware: see Appendix A.

**Episode number overflow (found by the T3.7 corpus).** The SxxExx scanner
accumulated digits in an `int` without a bound, so a name such as
`Show S99999999999E1` overflowed (undefined; it read back as season
1215752191), and the episode patterns used `atoi`, which returned -1 for
`Episode 99999999999999999999`. A bounded `read_number` now refuses numbers
beyond `INT_MAX`: the scanner skips that token and an episode pattern
counts it as episode 0. Every other name in the corpus decodes as before,
and `INT_MAX` itself still decodes. `tests/run-shrink-giants.sh` adds nine
boundary names (the scanner's overflow aborts the UBSan build of the
previous code).

**T1.8 on hardware (2026-10-20, Pavo).** Twenty-two `libzune-hardening-filler`
tracks (21 of 400,002,655 bytes, the last sized to the remaining space and
journaled as they were created) took the HD from 8,759,814,811 to 27,291
bytes free; each 400 MB filler used 400,015,360 bytes (16 KiB allocation
units). A 2 MB track was then refused at creation with StoreFull after one
attempt (`track create attempt 1/3 failed ... (StoreFull 0x200C)`, no second
attempt), `zune_autopsy` returned 0x200C and the error named StoreFull. A
photo prepared to 57,558 bytes behaved the same. All fillers were removed:
free space returned to exactly 8,759,814,811 bytes, and the inventory and
ZMDB counts equalled the 309-object baseline. This run also showed the photo
error naming the prepared copy, fixed in the T3.1 follow-up above.

**T1.8.** `video.c` already triaged on the saved code but did not restore it,
so `zune_autopsy` read the cleanup's OK after a failed video send; it now
restores it like the track and photo loops. Photo failures also set the error.

**T1.9.** On a cache miss the folder list is read from the device; if that
read is incomplete the upload fails instead of creating a possible duplicate
(the ARTIST_REUSE rule: failed inventory never justifies creation). Matching
stays by exact name across all folders, as the cached lookup always did, so an
album named like a system folder still lands in it. The cache is still
process-global and survives `zune_sever`, so a reconnect within one process can
hit a stale folder ID; that is T3.4.

**Pavo hardware run, 2026-10-19 (`37a8c6c`, Zune HD, agent-run).** A scratch
harness linked the static library and used only the normal connect path.
- Read-only survey, two breach/sever cycles in one process: both authenticated,
  235 objects each time, live ZMDB scan 180 tracks / 7 photos / 11 albums /
  6 artists, 9 folders. The same survey linked against the pre-hardening
  library (`850dcca`) produced identical output. Both builds lose the first
  OpenSession of every breach to a 10 s timeout and recover through the
  existing reset-and-retry; that delay is pre-existing HD behavior.
- T1.9: a PNG with `ZUNE_FFMPEG` pointing nowhere failed with
  "Photo is not a JPEG and could not be converted" and the inventory stayed
  235. Session 1 sent two JPEGs to a new `libzune-hardening-album-*`: one folder
  was created after the device lookup, both photos in it (235 -> 238).
  Session 2, a new process with an empty cache, sent a third photo: the lookup
  found the album on the device and reused it, still one folder (-> 239).
- Cleanup deleted only those four new objects and restored the exact
  235-handle baseline. Connect-time free space is 48 KiB lower than before the
  photo cycle, consistent with a FAT directory or the firmware database
  keeping a cluster.
- Track roundtrip in one session (`zune_smuggle_track_tagged`, extract, purge):
  the upload succeeded and extracted byte-identical; the HD added two companion
  objects during the session, which were gone at the next connect, leaving the
  exact baseline. On this HD a raw tagged send reads back its artist as
  "Unknown Artist", so `zune_verify`'s artist check fails; Zuuned links artists
  through artist objects instead. The retained HD artwork test objects were
  not touched.

**T2.2.** All 288 stderr writes outside `mtpz.c` now go through `ZLOG`
(`src/wiretap.h`, `src/wiretap.c`). Ground rule 0.3 allows only T1.6/T1.7
changes in `mtpz.c`, so its 42 `[mtpz]` writes still go to stderr directly and
bypass a wiretap; that needs an author decision. Levels follow meaning:
retries/fallbacks/mismatches SHOUT, failures SCREAM, lifecycle TALK, and
protocol detail, per-item progress, the device serial and no-art thumbnail
lookups WHISPER. The per-transfer `[ptp] sent N MB (MB/s)` summary stays TALK
(project throughput telemetry). Full tool command lines are no longer logged
at all. Multi-call dumps (supported-property lists, PTP hex dumps, the ZMDB
research tables) are now single messages; the research scan loses its
cosmetic blank lines and every row carries the `[zunedb]` tag. The macOS
`driverkit_usb.c` received the same mechanical conversion but cannot be
compiled on this host. Later on October 20 the user approved the `mtpz.c`
part as well (validation round below): its 46 writes now go through `ZLOG`
too (`f52cd2f`).

**T2.6.** The identical GetDeviceInfo branches were already collapsed in T1.5.
Build warnings dropped from five to the two in `mtpz.c` (the `char` overflow
and the unused `mtpz_encryption_cipher`), left at first because rule 0.3
froze that file apart from T1.6/T1.7. The user later approved both fixes
(`063c31c`, validation round below): the build has no warnings and `mtpz.o`
no -Werror exception.

**T2.4.** Neither the probe nor libav was at fault. A locally built FFmpeg
6.1.2 (libavformat 60.16) passed all 64 native and 64 ffprobe-fallback checks.
The check fails whenever the test runs in a non-UTF-8 locale (C, POSIX, or no
locale variables, as in a bare `ubuntu:24.04` container): bash 5.2 and 5.3 then
expand `$'\u00a0\u3000'` to the literal ASCII text `\u00A0\u3000`, so the
"whitespace-only" artist fixture held real text, which the probe correctly
reported. Reproduced on this host with FFmpeg 9 under `LC_ALL=C` (63/64). The
fixture now writes the UTF-8 bytes with printf octal escapes; the assertion is
unchanged, and 64/64 pass for both paths in C, POSIX and C.UTF-8, and under
FFmpeg 6.1.2 in the C locale.

**T2.3.** `make CFLAGS+=-Werror` previously replaced the Makefile's own flags
(includes included); the Makefile now uses `override CFLAGS +=`, so the spec's
command works, and `mtpz.o` alone adds `-Wno-error` for its two frozen
warnings. Seeds come from `tests/fuzz/make-seeds.py` (synthetic databases, the
review's T1.4 crash inputs, T1.5 wrapping-count replies); no device dump is
committed. The MTP harness also drives object info, representative samples,
UCS-2 strings and GetDeviceInfo; that parser moved to `src/devinfo.c` because
libFuzzer's coverage tables keep all of `device.c` linked. Local 60 s runs:
1.26 M ZMDB and 71.6 M MTP executions, no findings. The workflow itself has
not run on GitHub; each step passed when simulated here. A clang `-Werror`
build on this host fails only on `_Generic` macros in its newer glibc headers
(not present in Ubuntu 24.04's glibc), plus one clang-only string-plus-int
warning in `hd_tunnel_probe.h`, now rewritten with identical behavior.
`tests/run-audiobook-gate.sh` is not a hardware test after all: linker wraps
make any USB attempt abort, Zuuned's `ci/check.sh` already runs it, and it
passes (29 checks), so CI runs it with the other suites.

**Zuuned check, 2026-10-19 (`9d69899`).** The application on its
`hardening/0.3.0` branch builds against this library, and its full
`ci/check.sh` passes (53 steps, including the libzune suites it runs and the
public-source export/auth checks). Launched against the attached Zune HD, it
authenticated, scanned 180 tracks / 7 photos / 11 albums / 6 artists (matching
the libzune survey) and completed its video, photo, storage and spoken-media
inventory with no device writes. One earlier launch was terminated by the agent
about 20 s in, during that read-only inventory; its log shows no send or
delete, and the next launch recovered through the usual OpenSession reset.

**Extended Pavo pass, 2026-10-19.** A second harness drove the public API on the
HD. Read-only: wiretap levels on a live session (the serial line only at
WHISPER), identity/storage, all three ZMDB paths plus a raw dump, every MTP
listing (180 tracks = 170 MP3 + 10 WMA = ZMDB), album references, album art,
photo thumbnails through the download-and-resize fallback (all 7 photos; the
T1.1/T1.3 path), extracts and track state. Writes, each removed in-session:
`zune_arm_audio` FLAC→MP3 + `zune_retag` + tagged send, verify, rename, state,
byte-exact extract; `zune_arm_episode` HD profile + named video with poster,
byte-exact extract; a real JPEG sent unchanged with ffmpeg unavailable. Each
pass ended with the exact 235-handle inventory and identical ZMDB counts in a
fresh session. Finding: every H.264 profile failed on 4:4:4 sources because x264
baseline refuses them; all profiles now force `-pix_fmt yuv420p`
(`tests/run-transcode-profiles.sh`). One album on the HD stores a 3000×3000 PNG
as its representative sample (written by an earlier sync); the library returns
it unchanged.

Later the same day, also on Pavo, each followed by an exact-baseline check in a
fresh session: a 1024×768 PNG converted by real ffmpeg (480×360 JPEG on the
device); a 24 MB track through the chunked path (`[ptp] sent 24 MB in 4.0s`),
extracted byte-identical; playlist forge/rewire/purge, created as 0xBA05 with
`ZUNE_PL_FMT=0xBA09` set (the knob is ignored); `zunetool podcast-test` and
`zunetool audiobook-test` (creation, ID reuse, exact audio/cover readback,
cleanup, inventory preserved); `zunetool torture` (20/20 sends, no retries,
all removed, finalize OK); public-API examples dynamically linked against
`libzune.so.0`; and the read-only `zunetool info/list/playlists/podcasts/
media-capabilities/audit/refs` commands (`audit`: 180 entries, 0 suspicious).
The old and new ZMDB parsers again produced identical output for a fresh dump.

**Draco pass, 2026-10-19.** A Zune 80 (family Draco, `0x03070014`; 1,277 MTP
objects; ZMDB Classic v2 with 708 tracks, 181 videos, 84 photos, 43 albums,
27 artists, 5 playlists) ran the same harness, at the user's request, after
Zuuned had connected to it read-only. Every session ended with the exact
1,277-handle inventory and identical ZMDB counts in a fresh session.

- Read-only: 27 of 27, including the 708 ZMDB tracks against 723 MP3 objects,
  all 43 album arts, photo thumbnails and exact-size extracts. The old and new
  ZMDB parsers produce identical output (1,837 lines) for this database, which
  also passes the strict loader in `run-zmdb-hostile.sh`.
- Writes: the FLAC→MP3/retag/send/verify/rename/state/extract round trip, the
  device's own video profile (H.264, format 0xB982) and the Zune 30 WMV profile
  (0xB981), a TV episode whose series/season/episode read back, a 24 MB track
  (13.7 MB/s), JPEG without ffmpeg, PNG through ffmpeg, PNG without ffmpeg
  refused with the inventory unchanged, two sessions sharing one photo album
  (the second session found the folder), playlist forge/rewire/purge,
  `podcast-test`, `audiobook-test` (schema 0x12, exact audio and cover) and
  `torture` (20/20, finalize OK).
- Album objects, `zune_brand` art and album references work, but the firmware
  has no artist objects: GetObjectPropsSupported(0xB218) answers 0x2016, and
  `zune_forge_artist` skips them as before (unchanged code). Its ZMDB photo
  rows carry no filename.
- Format matrix (fixtures generated locally): FLAC (mono with cover, 24-bit
  96 kHz 5.1, Unicode tags), WAV, AIFF, ALAC, AAC with cover, Vorbis, Opus,
  WMA, MP3 (VBR with ID3v2.4 and PNG art, CBR, mono 22 kHz, untagged), raw
  ADTS AAC and MP2 through `zune_arm_audio`/`zune_retag` and Zuuned's
  `transcodetool`; native WMA as 0xB901. H.264 1080p (5.1, two audio tracks,
  subtitles), HEVC Main10, VP9/Opus, MPEG-4 Part 2, ProRes 4:2:2 10-bit, H.264
  4:4:4, portrait 1080×1920, 852×478 without audio, WMV2, interlaced MPEG-2 TS,
  720p60 and AV1 through both video paths and both Draco profiles. JPEG
  (baseline, progressive, EXIF-rotated, CMYK, 6000×4000), PNG (alpha, 16-bit,
  Unicode name), WebP, animated GIF, BMP, TIFF, HEIC and AVIF photos. All 99
  items (33 tracks, 49 videos, 14 photos, 3 album arts) read back
  byte-identical (photos against libzune's own prepared JPEG), and the
  firmware database listed each track and video. Non-BMP
  characters are stored as U+FFFD, by design. With FFmpeg 9 an EXIF-rotated
  photo arrives upright. Outputs were 320×240 Constrained Baseline level 2.1
  4:2:0 with stereo audio; a 60 fps source stays 60 fps in both pipelines,
  above the Zune 80's 30 fps video specification. No playback was observed on
  the device screen.
- Findings, now fixed with tests that fail on the previous code:
  `zune_arm_photo` refused animated GIFs (ffmpeg will not write several frames
  to one JPEG; it now takes `-frames:v 1`); `zune_snap_thumb` produced nothing
  for clips under its 10-second seek (FFmpeg 9 exits 234; FFmpeg 6.1.2 exits 0
  without writing the file) and now retries from the start and removes a
  failed output (`tests/run-media-prep.sh`, also under FFmpeg 6.1.2); and
  `zune_zmdb_read_payload` could leave the device's OK response unread.
- The ZMDB response defect wedged the device once. The read after a photo
  upload ended on a packet boundary, so the single 100 ms drain consumed only
  the zero-length packet; the next GetObjectHandles took that OK as its answer,
  its own data stayed queued, and every later command timed out. The session
  had already removed its earlier uploads; the photo and its album folder were
  removed by the next session and the baseline verified. The response is now
  read through ZLPs for up to a second (`tests/run-zmdb-transport.sh`). Rerunning
  the same uploads, the read at that point was 414,720 bytes (810 × 512): the
  log shows the zero-length packet, then the consumed OK, and the pass
  completed. The read and write passes were repeated with the fix.
- Zuuned (app) findings: its sync treated raw ADTS AAC and MP2 (any 0xFFE
  sync) and any ID3-tagged file as MP3 and would send them unconverted; the
  app now requires Layer III audio after its tags. Its transcoder writes to
  predictable `/tmp/zuuned_*_<pid>` names (the T1.3 problem, app side), which
  is not changed here. Session starts still sometimes need the existing
  OpenSession reset.

**Keel pass, 2026-10-20 (`f096e72`, Zune 30, agent-run).** A Zune 30 (family
Keel, `0x00000001`; a 127.8 GB drive with 95.4 GB free; 1,484 MTP objects;
ZMDB Classic v2 with 1,145 tracks, 132 videos, 52 photos, 82 albums,
46 artists, one playlist and one photo album) ran the same harnesses at the
user's request, with Zuuned closed. Every batch of writes was followed by a
fresh-session check of the exact 1,484-handle inventory and the ZMDB counts,
including the full-device run.

- Read-only: 26 of 27. Album art came back for 79 of the 82 albums; the other
  three store an empty RepresentativeSampleData, so there is no art to return.
- Writes: write 18/0, write2 9/0, write3 10/0 and playlist 7/0; videos used
  the device's profile 0 (WMV, format 0xB981). The harness now gives the
  device file the prepared file's extension (`.wmv` here); it had hard-coded
  `.mp4`, which suits only the H.264 profiles. `zunetool podcast-test`,
  `audiobook-test` and `torture` (20/20; CleanDataStore OK, 0x9202 answers
  0x2005 as before) passed.
- Album objects, art and references work. As on Draco, the firmware has no
  artist objects (GetObjectPropsSupported(0xB218) answers 0x2016) and
  `zune_forge_artist` skips them, which the album pass counts as its one
  failure.
- T3.3: an album and a playlist referencing a nonexistent track were refused
  with 0xA804, removed and reported. T1.9 and T3.4: a PNG with ffmpeg
  unavailable was refused with the inventory unchanged; two photos in one
  session shared one new album folder, and a second handle in the same
  process read the folders and reused it. T1.6 and T3.7: three breaches in
  one process all authenticated, keys loaded on the first and third only.
  T3.5: extracting a missing object returned `ZUNE_BLOWN` with its own error
  (autopsy 0x2009), kept through `zune_autopsy` and the getters and cleared by
  the next successful call.
- T3.6: aborts at 1.5 s stopped an upload after its first 1 MiB chunk and a
  download at 20 MiB; they returned 2.1 s and 0.3 s after the abort with
  autopsy 0x201F, and the session kept answering. In the same session the
  inventory listed one extra handle in the album range (`0x060006CF`) whose
  GetObjectInfo answers 0x2002. The Keel lists such a handle after ordinary
  track uploads too (the write pass saw `0x060006C9`), and it was gone in the
  next session, which matched the baseline; the abort harness counts that
  same-session difference as its failure.
- T3.2: a 400,002,655-byte track uploaded in 29.9 s (13.4 MB/s, twice the HD's
  rate) while peak RSS rose from 26,720 to 27,616 KiB.
- T1.8: 239 `libzune-hardening-filler` tracks (400,002,655 bytes each except
  the last, sized to the remaining space; journaled as they were created) took
  the drive from 95,442,370,560 to 0 bytes free in 1 h 50 min (1 h 52 min
  with the cleanup); the first 400 MB filler used 400,064,512 bytes and the
  other 237 full ones 400,031,744 (32 KiB allocation units). A 2 MB track was
  then refused at creation with StoreFull after one attempt, `zune_autopsy`
  returned 0x200C and the error named StoreFull; a photo prepared to 57,558
  bytes behaved the same. All fillers were removed and the inventory and ZMDB
  counts equalled the baseline. Free space came back 32,768 bytes below the
  start, and the first write pass had lowered it by 64 KiB for good. No object
  was left behind either time, which fits a FAT directory keeping the clusters
  it grew into.
- OpenSession: 19 of the 28 harness sessions needed the existing
  reset-and-retry (the first breach in 14 of 22 processes, and 5 later ones),
  and every one recovered. Bare reconnects (breach, read free space, sever;
  two processes of six rounds and 12 single-round processes) needed one
  reset in 24 with the 0.2.0 library and none in 24 with 0.3.0, so this is not
  a 0.3.0 regression; the one bare reset came right after a verify pass.
- Offline (device data, kept local): on this device's 396,732-byte database
  the ZMDB parsers before and after T3.7 produce identical `zune_infiltrate`
  dumps (1,460 lines) under ASan and UBSan, and the old and new filename
  decoders agree on all 132 of its video names. The pre-hardening parser
  matches too (validation round below).

**Draco pass, 2026-10-20 (`4fd88f8`, Zune 120, agent-run).** The Draco unit
from the 2026-10-19 pass reports 119.8 GB in all (a Zune 120; the user calls
it the Zune 80). With 64.7 GB free, 1,287 MTP objects and ZMDB Classic v2
holding 709 tracks, 182 videos, 84 photos, 44 albums, 28 artists, six
playlists and two photo albums, it ran the Zune 30 sequence at the user's
request (the harnesses with Zuuned closed), then a sync and removal through
Zuuned. Every session ended with the exact 1,287-handle inventory and
identical ZMDB counts in a fresh session, including after the full-device run
and the Zuuned test.

- Read-only: 27 of 27, with all 44 album arts. Writes: write 18/0 (the
  device's H.264 profile, format 0xB982, `.mp4`), write2 9/0, write3 10/0,
  playlist 7/0; the album pass again fails only on the missing artist objects
  (0x2016). `zunetool podcast-test`, `audiobook-test` and `torture` (20/20)
  passed.
- T3.3: an album and a playlist referencing a nonexistent track were refused
  with 0xA804, removed and reported. T1.9 and T3.4: a PNG with ffmpeg
  unavailable was refused with the inventory unchanged; two photos shared one
  new album folder, and a second handle in the same process reused it. T1.6
  and T3.7: three breaches in one process authenticated, keys loaded on the
  first and third only; each breach needed the OpenSession reset. T3.5: the
  missing-object extract returned `ZUNE_BLOWN` with autopsy 0x2009, kept until
  the next successful call.
- T3.6: the upload stopped after its first 1 MiB chunk and the download at
  23 MiB; they returned 2.2 s and 0.3 s after the abort with 0x201F, and the
  session kept answering. As on Keel, that session listed one extra handle in
  the album range (`0x060004FE`, then `0x060004FF`; the write pass showed
  `0x060004F8`) whose GetObjectInfo answers 0x2002; each was gone in the next
  session.
- T3.2: a 400,002,655-byte track uploaded in 29.4 s (13.6 MB/s) while peak RSS
  rose from 26,800 to 27,824 KiB.
- T1.8: 162 `libzune-hardening-filler` tracks (400,002,655 bytes each except
  the last, sized to the remaining space; journaled as they were created) took
  the drive from 64,716,865,536 to 0 bytes free in 1 h 13 min; each 400 MB
  filler used 400,031,744 bytes (32 KiB allocation units). A 2 MB track was
  then refused at creation with StoreFull after one attempt, `zune_autopsy`
  returned 0x200C and the error named StoreFull; a photo prepared to 57,558
  bytes behaved the same. Removing the fillers took about a minute, free space
  came back to exactly the starting value, and the next session matched the
  baseline inventory and ZMDB counts.
- OpenSession: 33 of the 50 logged harness sessions needed the existing
  reset-and-retry, and every one recovered; six bare reconnects needed one,
  and each of Zuuned's two connects needed one.
- Offline (device data, kept local): on this device's 422,564-byte database
  the ZMDB parsers before and after T3.7 produce identical `zune_infiltrate`
  dumps (1,051 lines) under ASan and UBSan, and the old and new filename
  decoders agree on all 182 of its video names. The pre-hardening parser
  matches too (validation round below).
- Zuuned (`ae598da`, built on `4fd88f8`; `zuuned --version` named them
  `2f08681` and `ae7e97c`, their hashes before the history rewrite below)
  connected through the same reset-and-retry and read the database. The queue
  was empty and no podcast rows appeared, although the user's settings enable
  podcast auto-queue and cleanup. A five-track FLAC album and a 2 min 53 s TV
  episode, neither on the device, were queued through the window and synced in
  21 s with no failed attempt: the tracks went over as MP3, the album object
  with its 58,929-byte cover (the artist object skipped with 0x2016), and the
  episode as H.264 (format 0xB982, 19,556,282 bytes at 12.8 MB/s) with a
  1,048,463-byte poster that the device then returned. Podcast cleanup removed
  nothing. A four-photo album whose photos are all on the device queued nothing
  ("all 4 already on the zune"). After the sync's re-index Zuuned reconnected,
  listed both items and removed them from its device pages (five tracks, then
  the video), and ejected; the album object, emptied by the track deletes, was
  gone in the next session. With Zuuned closed, the inventory and ZMDB counts
  equalled the 1,287-object baseline; free space stayed 98,304 bytes (three
  clusters) below it, like the Zune 30's directory growth.

**Validation round, 2026-10-20 (agent-run).** Before any push, the user had
eight review agents check the work: Tier 1, Tier 2 with the release, Tier 3,
the global rules, builds and tests, the hardware evidence, code quality, and
Zuuned's use of the library. The agent checked each finding against the code
and the saved outputs, fixed what held up, and took three decisions to the
user (the `mtpz.c` conflict, the history rewrite, the Zuuned bugs).

- Deviations not recorded until now. T1.6 and T1.7 landed after Tier 2
  (rule 0.5). Tier 3 began (`8b97397`, T3.6, October 19 23:49) before
  Appendix A had any T1.8 result or a Keel column; T1.8 first passed after the
  last Tier 3 commit, on Tier 3 code. T2.3 and T3.1 have two commits each. The
  delegation entry above covered agent-run gates and finishing Tier 3, not the
  order.
- Wire changes that no task text names, now marked as exceptions to rule 0.3:
  the GetZMDB read through ZLPs and late answers (`d889c8d`, found on the
  Draco), the PTP-over-USB cancel after an aborted or failed data phase
  (`dcb6717`, extended in `bb93375`; its evidence is the T3.6 row) and that
  cancel's 10 s polling bound (`2efb45e`). A failed header write in split
  framing and a failed first chunk in combined framing still return without a
  cancel. `zune_get_brand_limits` (`c81bcfc`) is a public function outside the
  spec, added for Zuuned's device covers.
- `mtpz.c`: rule 0.3 allows only T1.6/T1.7 there while T2.2 and T2.6 name the
  file. The user approved the logging and the same-byte cast: `mtpz.c` logs
  through `zune_wiretap` (`f52cd2f`; the certificate hex dumps at WHISPER) and
  builds without warnings or a -Werror exception (`063c31c`). Its crypto code
  is unchanged.
- T1.8 on hardware: every StoreFull refusal came at creation (SendObjectInfo),
  where 0.2.0 already triaged on a fresh response, so the hardware runs show
  the attempt count and autopsy but not the data-phase path T1.8 changed;
  `tests/send-retry.c` proves that path.
- Fixed in code, each with a test that fails on the previous code:
  - CI: both fuzz harnesses stopped linking after T3.2/T3.5 (`3dc0980`; a fuzz
    smoke run now sits in the suite loop). libzune.so named its libraries
    before its objects, so Debian/Ubuntu's default --as-needed dropped them and
    the library could not load, failing run-symbols.sh; `make install
    PREFIX=...` installed a pkg-config file for the default prefix
    (`17c0ccc`).
  - Strict (podcast, audiobook) uploads retried a creation answered OK without
    a handle and could create a second object (`18b2070`).
  - An album whose SetObjectReferences got no answer was deleted, which on a
    Zune HD also deletes its tracks; it is now kept and reported (`d011a72`).
  - `zune_dump_raw` kept the drain the shared reader lost in `d889c8d`
    (`1a2c100`, `e1a28ba`).
  - Tool lookup searched relative PATH entries (`bd02a72`). `zune_snap_thumb`
    could return an older file as the new thumbnail (`41a392b`). Two
    successful calls left an inner error set (`d615992`). `zune_get_folders`
    returned a partial list when the device stopped answering (`08a86c8`).
    Ten wrappers leaked a reply after a failed response, or handed it back
    with the failure (`15695c0`, pre-existing). Classic ZMDB discovery read
    index bytes as descriptors and could reject a whole scan (`1f730a9`). The
    cancel's status polling could last almost two minutes (`2efb45e`).
  - Tests and tidying: UBSan reports fail every suite (`6174a8e`), T1.4's
    zero-length case (`77e91b4`), and the T3.7 leftovers (`de21caa`).
- Evidence corrections, made above: the Draco download abort stopped at
  23 MiB (24,117,248 bytes), not 24; the Pavo T3.6 cell mixed total durations
  with the time after the abort; Keel's fill reached 0 bytes free in 1 h 50 min
  (1 h 52 min with the cleanup), its first filler used one cluster more than
  the rest, and its bare reconnects ran as two six-round processes plus 12
  single-round ones. Keel and the HD were verified once per batch of writes,
  not after every session; only the scripted Draco run verified after every
  step. The verify compares the handle set and ZMDB counts, so it would not
  see an object changed in place. "Old and new parsers" in the Keel and Draco
  entries meant the parsers before and after T3.7; the pre-hardening parser
  (`3f19634`) also gives identical field hashes (full and legacy scans) on all
  seven saved databases (Keel, three Draco, three Pavo).
- History rewrite (the user's decision): an audiobook title from the user's
  library appeared in two commit messages and two documents. libzune from
  `d2130b1` and Zuuned from `dccb397` were rebuilt with the title removed and
  the submodule pins remapped; trees are otherwise identical, and authors and
  dates are kept. Hashes cited before the rewrite map as: libzune `82dace6` →
  `d2130b1`, `eb0ddcc` → `f096e72`, `ae7e97c` → `4fd88f8`, `e83da98` →
  `3be2950`; Zuuned `b044e8c` → `dccb397`, `8b61305` → `d006b81`, `2f08681` →
  `ae598da`, `34b77aa` → `ce3d255`. Session logs from before the rewrite show
  the old hashes.
- Not run: the two `zunetool` CLI suites, because a regression there would
  open the attached Zune (`tools/zunetool.c` is unchanged in the range). The
  macOS paths (`driverkit_usb.c`, the sips and Homebrew branches, the install
  name) were changed but never built.
- Rerun after the fixes, on the Draco (libzune `d09aaa0`, scripted harness
  pass, 2026-10-20). The fresh-session verify now also compares a field
  digest of every ZMDB object (a sum of per-object hashes, so record order
  does not matter), so an object changed in place fails it. Every verify
  matched the baseline. read 27/0, write 18/0, write3 10/0, album pass (the
  artist object skipped as before), playlist 7/0, partial 3/0, T1.9 and T3.4
  photos pass, T1.6 pass (three breaches, so MTPZ sign-in works with the new
  logging), errcheck 4/0, podcast and audiobook gates pass, torture 20/20,
  T3.6 upload and download aborts with 0x201F and the session answering,
  400 MB stream at 13.6 MB/s with RSS +1 MiB, reconnects 6/6. write2 failed
  once on the 24 MB readback: see the next item. 30 OpenSession resets across
  the pass, all recovered. Zuuned built on this library (app `ca157b4`) then
  synced three tracks of one FLAC album (with the album object and its art)
  and one podcast episode in 13 s with no failures and no podcast removed,
  removed them through its device pages and ejected; the verify afterwards
  matched the baseline exactly.
- Read flips on this Zune 80 (hardware, found by that rerun). Downloads now
  and then return one byte with bit 7 set, with the length exact and no USB
  error: nine flips in 61 downloads of a 24 MB track, about one per 160 MB
  read, and each of the eight whose position was recorded sat at byte 120 of
  a 512-byte USB packet. The stored copies were intact.
  A second download of each was identical, and 10 separate uploads read back
  three times each had no byte stored wrong, so writes are not affected. The
  fixed position inside the packet rules out libzune (its receive path reads
  1 MiB at a time and copies whole buffers) and host memory (root port, no
  hub; the kernel logged no xHCI, EDAC or MCE errors). The same afternoon a
  Zune 30 on the same root port, with the same file and harness, returned
  all 130 downloads identical (one upload read 100 times, then 10 uploads
  read three times each, about 3.1 GB); at the Zune 80's rate about 19 would
  have differed. Its baseline (1,484 objects, counts, fields and free space)
  was exact afterwards, and a Zune HD on the same port also returned 130 of
  130 identical. So the host's USB path is sound and the fault is in the
  Zune 80's USB send path. MTP carries no checksum, so any read from that
  unit (ZMDB scans, pulls into a library) can carry a flipped bit; writes to
  it are not affected. The skill harness now downloads again on a mismatch
  and counts a read flip when the second copy is identical, and its `reread`
  mode measures the rate.
- Rerun after the fixes on the Zune HD (libzune `73cec58`, whose code is
  `304b023`'s; 2026-10-20). Every verify matched the 290-object baseline:
  read 27/0, write 18/0, write2 9/0 (the 24 MB readback byte-identical),
  write3 10/0, album pass, playlist 7/0, T3.3, the T1.9 and T3.4 photos, T1.6
  (three breaches), errcheck 4/0, the podcast and audiobook gates, torture
  20/20, upload and download aborts with 0x201F and the session answering, a
  400 MB stream at 6.7 MB/s with RSS +1 MiB, and reconnects 6/6. Two
  in-session checks failed as on the earlier HD runs, both firmware
  behavior: deleting the test album also deleted its track, so the track's
  own delete answered 0x2009; and creating the T3.3 album made the firmware
  add an "Unknown Artist" artist object of its own (format 0xB218), gone by
  the next session. The skill harness now names both and still fails
  anything else (album 11/0 and T3.3 3/0 when rerun). Zuuned (app `9645321`)
  then synced three tracks of one FLAC album, creating a "Various Artists"
  artist and the album with its art, and one podcast episode, in 15 s with no
  failures. After they were removed through its device pages and the Zune
  ejected, the firmware had dropped the emptied album and the new artist,
  and the verify matched the baseline exactly.

**Follow-up review fixes, 2026-10-20.** These are software fixes on
`hardening/0.3.0`, with no new device writes or hardware claims:

- T1.1: thumbnail generation rejects input/output aliases before deleting any
  output or resolving ffmpeg. Regression cases preserve the original bytes for
  identical paths, equivalent spellings, symbolic links and hard links,
  including missing ffmpeg. The same-path case failed before this fix.
- T1.4: Classic v2 descriptor 0 bounds the table before its descriptors are
  read. A complete 1,105-byte, 33-descriptor fixture now returns its track,
  album and artist through both parsers; it failed the old unconditional
  96-slot size check. Truncated tables, indexes and records remain errors;
  HD and unknown versions retain the full-table requirement. Physical minimum
  Classic database size was not measured.
- T3.1: cancellation or disconnect after an unanswered creation keeps the
  uncertainty warning and original autopsy. A later attempt being removed
  cannot prove the first attempt left nothing behind. Cancelling before any
  creation, or after proven cleanup with no earlier uncertainty, does not
  invent an orphan. Zuuned uses the preserved warning to retain recovery.
- The API contract documents thumbnail alias refusal. Regenerate the API
  reference and TOC together after these source changes; CI checks both.
- Verification: warnings-as-errors library, CLI and examples build; all 46
  selected offline suites pass (the two CLI refusal suites that could open an
  attached device are excluded). Focused sanitizer gates now have 38 tool,
  57 ZMDB and 24 upload checks. The rebuilt Zuuned passes 121 spoken-sync,
  37 recovery and 45 music-sync checks with isolated profiles and no USB.
  Both parsers also completed 60-second sanitizer fuzz runs without findings:
  1,429,132 ZMDB inputs and 76,300,588 MTP inputs. The API reference and TOC
  freshness checks pass.
