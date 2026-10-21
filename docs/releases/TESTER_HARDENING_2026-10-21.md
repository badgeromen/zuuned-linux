# Private hardening AppImage, October 21, 2026

The user requested the latest private Zuuned and libzune hardening work in an
AppImage, testing through Zuuned with the attached HD, and comparison against
the open GitHub issues. No publication, issue changes or branch merge occurred.

## Artifact and provenance

- App: `100527be8e2208d869a89de1361a00c4b9dd7dcd`, `hardening/0.3.0`.
- libzune: `09fa9221fd5f59e5061b74251e1eef930b8371a6`, `hardening/0.3.0`.
- Both trees were clean and matched their fetched Gitea branches. The app pin
  matches the actual library commit. The app remains version 0.2.0; the linked
  library reports 0.3.0. This build does not claim a new application version.
- Artifact: `build-appimage/dist/hardening-0.3.0-100527b/Zuuned-0.2.0-g100527be8e22-20261021T044058Z-x86_64.AppImage`.
- SHA256: `8c82bfd6e59b667e1235138b0a22cf60dbc98346ce28219f872ef0291f617912`.
- Sidecars retain the checksum, build JSON, environment and tool lock.
- Evidence: `build-appimage/dist/hardening-0.3.0-100527b/evidence/`.
- Snapshot: sibling directory `zuuned-hardening-appimage-100527b-source`.

The filename timestamp is the app commit's `SOURCE_DATE_EPOCH` (04:40:58 UTC),
not packaging completion (04:53:40 UTC). The Git-free snapshot embeds exact
commits and clean flags; branch-name fields are null.

## Packaging verification

`bash packaging/tests/run.sh` passed. The Debian 13 builder used Qt 6.8.2 and
glibc 2.41. The recursive ELF/AppDir inventory and copied-artifact checksum passed.

Immutable images:

- Builder: `sha256:41a9ffba8ef4d91e74d30cb66dac2b96ae11ee1269b88a444fbb55ce2def1c6e`.
- Runtime: `sha256:eb2ff7c4a1aa628451467deefa5af12dca94de499ca0f2e2153c608714e8caf9`.

The first packaging attempt failed because linuxdeploy discovered an ARM Qt
plugin in the shared tool directory. Compilation had passed. Retrying with a
separate x86_64-only tool cache succeeded, with the same source and checksum-pinned
tools. The failed log is retained as `candidate-build-mixed-tools-failed.log`.
Keep separate architecture tool caches when reproducing this build.

The exact AppImage passed the isolated runtime container with networking disabled,
USB hidden, software OpenGL, null audio and disposable profiles. Results are in
`runtime/runtime-PCZT8xz6/PASS.txt` and `photo-builder-PASS.txt`. These cover actual
onboarding, native desktop choosers and import, Appearance, debug-report export,
bundled media tools, private logs, shutdown, SQLite integrity and creation/reopen
of nested photo albums. They do not certify physical photo transfer or older
distributions. No copied-profile migration gate was supplied for this run.

## GitHub issue #1 and packaged GUI test

At review time, [Zuuned issue #1](https://github.com/badgeromen/zuuned-linux/issues/1)
was the only open application issue; libzune had no open issues. The reporter
used Ubuntu 26.04.1 LTS x86_64 and a Zune HD. Adding
`https://feeds.megaphone.fm/COMG6839484448` failed with
“Response exceeds the size limit.”

The live feed decompressed to 13,343,901 bytes, exceeding the former 8 MiB cap.
The selected branch allows bounded feeds up to 128 MiB and handles highly
compressed XML. Its parser accepted 3,104 episodes with none skipped. Existing
software fixtures cover large previews, compressed feeds and limit enforcement.

All actions below used the packaged GUI, not zunetool or injected service calls.
The native app was closed while idle. A separate XDG profile kept normal-profile
automatic queueing and cleanup out of the test. The connected device was the
16 GB Zune HD, Pavo, named ZUNE HD RED.

| Acceptance step | Observed result |
| --- | --- |
| Preview the exact reported RSS address | A Celtic State of Mind, artwork and 3,104 episodes appeared without the size error. |
| Subscribe and open the show | Complete catalogue count and episode list displayed. |
| Download one newest episode | Download completed despite the feed advertising enclosure length zero. |
| Play locally | GUI playback advanced past 14 seconds and was paused at approximately 31 seconds. |
| Select newest count 1 and queue | Exactly one episode entered the transfer queue. |
| Sync to HD | 74 MB payload sent; summary reported one podcast, zero failures, no cleanup warning, 13 seconds. |
| Cover transfer | 13,778-byte representative image accepted; the app displayed the cover from the device after reconnecting. |
| Reconnect and browse the device | Podcast inventory increased from 10 to 11. The new show contained exactly the selected episode, alongside the original two five-episode shows. |
| Repeat selection/add | Device badge appeared; the existing episode could not be selected again, and pressing the show + left the queue empty. |

The episode is “Why Celtic's Europa League campaign should be back in focus //
ACSOM // A Celtic State of Mind.” The feed advertises 73:09, while the downloaded
media probed as 77:51. The test episode remains on the HD for the user's physical
playback check. Existing device media was not removed. The AppImage was then
reopened with the user's original profile on workspace 1.

Saved captures include `issue1-preview.png`, `issue1-playing.png`,
`issue1-sync-complete.png`, `issue1-device.png` and
`issue1-duplicate-blocked.png`. Machine-readable evidence includes
`github-issue-1.json`, `issue1-sync-report.json` and
`issue1-repeat-plus-queue.json`.

## Limits and log observations

The reported preview failure is resolved in this candidate on the local
Arch/Hyprland desktop with packaged Qt 6.8.2. The original Ubuntu reporter's
retest remains outstanding. No issue was closed or commented on.

Reconnection encountered one OpenSession timeout; the existing reset/retry
recovered and authentication/inventory succeeded. Optional vendor finalization
operations returned 0x2005 while CleanDataStore succeeded. Logs also retain
thumbnail fallbacks, an unrelated MusicBrainz HTTP 503, and a PipeWire backend
initialization warning during local playback. Playback progression is observed;
audible output was not independently assessed.

This HD run does not establish physical HD screen/audio playback, completed-episode
cleanup, long audiobook transfers, HD artist-catalog delivery, interruption
recovery under deliberate cable removal, ARM compatibility or other Zune models.
Earlier hardening hardware evidence remains separate from these exact-artifact
results. Evidence/profile directories are private test artifacts, not release
assets to upload wholesale.

## Same-artifact Draco GUI follow-up

On October 21 the user connected the device they described as the Zune 80 and
requested the same test. The app/device identify it as Draco, model Zune 120,
with 119,820,943,360 bytes total storage. This records the observed identity
without assuming its physical enclosure or upgrade history.

The same AppImage and isolated test profile were used through the GUI. Baseline
inventory was 709 music tracks, 182 videos, 84 photos, 21 podcast episodes and
213 audiobook parts. The device browser displayed seven audiobook groups and
two podcast shows: Moonbound (15 episodes) and Nontendo Podcast (6).

The same downloaded ACSOM episode was selected with newest count 1. Sync passed
in 6 seconds with one podcast, zero failures, zero removals and no cleanup
warning. The log records 74 MB sent in 5.3 seconds (14.0 MB/s) and a 13,778-byte
cover accepted for item 251659509. After reindex/reconnect, the GUI displayed
the new show, exact episode and cover; inventory was 22 podcast episodes and
213 audiobook parts. The original two shows retained their 15/6 counts, and
music/video/photo counts were unchanged. The picker marked the test episode
on-device and excluded it; repeating the show + left the queue empty.

Two existing audiobook groups had missing covers before this transfer:
Chamber of Secrets and Goblet of Fire. Their representative-sample responses
contained zero image bytes (IDs 301990942 and 301990986). No repair or replacement
of those existing objects was attempted. The new podcast cover readback passed.

Initial connection and post-sync reconnection each encountered an OpenSession
timeout; the existing reset/retry recovered both times. Optional finalization
operations returned 0x2005, while CleanDataStore succeeded. These are retained
in the log, not described as a completely warning-free run.

Evidence is under the candidate's `evidence/draco/`: `appimage.log`,
`result.json`, `sync-report.json`, `repeat-plus-queue.json` and GUI captures.
The single test episode remains on the device for physical playback checking.
The normal user profile was reopened afterward on workspace 1. Automatic
cleanup stayed disabled in the test profile and no existing media was deleted.
This follow-up verifies podcast transfer/cover/inventory/dedup and audiobook
browsing; it does not certify new audiobook transfers, firmware playback/resume,
completed-episode deletion or full extracted-payload integrity.

## Full Draco media regression and audiobook replacement

The user then explicitly authorized testing every media category and removing
all audiobooks before a complete resync. This run used the same packaged binary
with the normal user profile, through GUI actions only. No competing USB tool
was opened. No application code changed during this test.

All seven source M4Bs were accessible, matched their recorded file sizes and
had readable audio/chapter metadata. Their 213 prepared chapter ranges were
consecutive, gap-free and covered each complete source timeline. This checks
source access and range coverage, not decoding every source sample.

Each device book was removed with its GUI confirmation. The browser then showed
zero books. All seven whole books were queued using their cover + buttons:

| Harry Potter book | Parts successfully resynced |
| --- | ---: |
| Chamber of Secrets | 20 |
| Deathly Hallows | 39 |
| Goblet of Fire | 39 |
| Half-Blood Prince | 32 |
| Order of the Phoenix | 40 |
| Philosopher's Stone | 19 |
| Prisoner of Azkaban | 24 |
| **Total** | **213** |

The sync completed in **2h 22m 38s**, with 213 successful parts, zero failures,
zero skips and no podcast cleanup warning. Logs contain 213 accepted cover
writes to unique part IDs. After reconnect, the device browser showed all seven
books and 213 parts with covers, including the previously missing Chamber of
Secrets and Goblet of Fire covers. The original M4Bs were not modified.

A subsequent mixed-category GUI sync completed in **37 seconds**:

| Category | Exact sample | Observed result |
| --- | --- | --- |
| Music | Yards of the Moon, **1101 Break**, Cyberpunk 2077: Radio, Vol. 5, FLAC | One track and its album/art transferred successfully. |
| Video | Bravest Warriors S01E01, **Time Slime**, 1080p x265 MKV | Native NVDEC decode and H.264/AAC encoding produced a 320×240 letterboxed MP4; 39,471,865 bytes plus poster transferred. |
| Photo | **Draco photo regression 2026-10-21.jpg**, an unchanged copy of the user's `106848.jpg` | Native 480px JPEG transfer succeeded; the new Hardening test album and thumbnail appeared in the device browser. |
| Podcast | Nontendo episode 181, **The ZELDA Nintendo Switch 2 Console is REAL?!** | 173,365,387-byte local download and 8,224-byte cover transferred successfully. |

The mixed summary recorded one item in each category, zero failures, zero skips,
zero podcast removals and no cleanup warning. Fresh post-reindex inventory:

| Category | Before this full test | After |
| --- | ---: | ---: |
| Music tracks | 709 | 710 |
| Videos | 182 | 183 |
| Photos | 84 | 85 |
| Podcast episodes | 22 | 23 |
| Audiobook parts | 213 | 213 |
| Audiobook groups | 7 | 7 |

Podcast show counts were Moonbound 15, Nontendo 7 and ACSOM 1, with all show
covers visible. No existing non-audiobook media was removed. Completed-podcast
cleanup was disabled for these transfers. At handoff the user's settings are
verified as auto-queue enabled, five episodes per show, and cleanup enabled;
no additional sync was started after restoring cleanup.

Local playback spot checks also exercised the original Philosopher's Stone
M4B and Nontendo episode through the book/microphone ribbon, advancing position,
30-second skips and pause. Time Slime rendered moving frames and Escape closed
its overlay; the music playhead advanced; the photo viewer rendered and its X
closed. Audio output was not independently heard. These desktop checks do not
prove physical Zune playback, chapter navigation or firmware resume. Full binary
extraction/comparison of the transferred media was not performed.

The post-mixed-sync connection needed one OpenSession reset/retry and recovered.
Expected optional finalization refusals and Classic size-discovery limitations
remain in the log. Unrelated source-file read failures for seven Paramore tracks
and provider HTTP failures were also recorded, without failing these transfers.
Two UI follow-ups were observed: audiobook conversion displays a video-specific
help message, and spoken selection remains enabled during active sync although
native admission rejects adding it until that sync finishes.

Private evidence is in `evidence/all-media/`: source inventory, chapter coverage,
queue snapshots, progress records, both sync reports, 213 cover-write records,
before/after GUI captures and final inventory/settings. The device session log
is `evidence/draco/normal-profile-launch.log`. Test media remains on the device
for handheld checks, and Zuuned remains open in the normal profile.

## Same-artifact Keel/Zune 30 full media test

The next user-requested test used the connected Keel/Zune 30, reporting 128 GB,
with the same running AppImage and normal profile. It preserved all existing
device content. Connection automatically queued five Moonbound episodes and
previewed sixteen completed-episode removals. Cleanup was disabled through
Settings before starting Sync, and the removal rows disappeared. Auto-queue
remained enabled with its existing count of five.

All admissions and transfers used the GUI. The mixed sync completed in
**11m 57s**, with **zero failures, skips or removals**, and no cleanup warning:

| Category | Sample | Successful items |
| --- | --- | ---: |
| Audiobook | Complete Chamber of Secrets headphone-surround M4B, converted into consecutive chapter parts | 20 |
| Podcast | Moonbound episodes 18, 17, 16, 15 and 14 | 5 |
| Music | Yards of the Moon, 1101 Break, FLAC | 1 |
| Video | Bravest Warriors S01E01, Time Slime, x265 MKV | 1 |
| Photo | Draco photo regression 2026-10-21.jpg, the same local test image with its filename retained | 1 |

Chapter ranges covered the complete 34,624,448 ms source timeline without gaps
or overlaps. All twenty unique audiobook part IDs accepted their 30,799-byte
covers. The video automatically selected Keel profile 0: NVDEC decode followed
by WMV2 320×240 output. The 37,849,215-byte WMV and 103,127-byte poster were
accepted. Photos used the native 480px JPEG path. Large transfers generally ran
around 14–15 MB/s; chapter conversion accounted for most of the elapsed time.

Fresh post-reindex inventory matched every expected count:

| Category | Before | After |
| --- | ---: | ---: |
| Music tracks | 1,145 | 1,146 |
| Videos | 132 | 133 |
| Photos | 52 | 53 |
| Podcast episodes | 27 | 32 |
| Audiobook parts | 21 | 41 |
| Audiobook groups | 2 | 3 |

The device browser displayed Chamber of Secrets with its cover and twenty parts,
alongside the original nineteen-part Philosopher's Stone and two-part test book.
Moonbound had 21 episodes and Nontendo 11, both with covers. The new Hardening
test photo album and thumbnail appeared. Post-sync reconnection hit one
OpenSession timeout; automatic reset/retry recovered and inventory reads passed.
Optional finalization refusals were followed by successful CleanDataStore.

Cleanup was restored to enabled after verification, with auto-queue/count-five
unchanged, and no further Sync was started. The app remains open in the normal
profile. The test does not establish physical handheld audio/video playback,
chapter navigation, firmware resume, completed-episode deletion or full binary
payload integrity. It adds actual Keel transfers to the earlier HD/Draco results,
not certification of every format or edge case.

Private evidence: `evidence/keel-all-media/`, including baseline, admitted queue,
chapter coverage, progress snapshots, sync report, final result, cover-write
records, session log and device-browser captures. No build, source-code change,
commit, push or publication was needed for this run.

## Large podcast catalogue in the normal profile

The user requested another large-feed import through Zuuned. A Celtic State of
Mind was already present in the normal profile with 3,104 episodes, so this test
added **Stuff You Should Know** using the RSS link from its
[official hosting page](https://omny.fm/shows/stuff-you-should-know-1):
`https://omny.fm/shows/stuff-you-should-know-1/playlists/podcast.rss`.

The same AppImage GUI preview and subscription successfully loaded **2,912
episodes**, with artwork and zero skipped items. An independent read of the
10,387,808-byte XML feed found 2,912 unique GUIDs; the saved episode GUID set
matched it exactly. The detail page supported newest/oldest ordering, scrolling
and a search for the oldest episode, **How Grassoline Works** (April 17, 2008).
A GUI refresh retained all 2,912 records and their exact local IDs, without
duplicates. The new show remains unstarred and open in the normal profile.

This test loaded catalogue metadata and artwork; it did not download thousands
of audio files or start another device sync. Existing queue/settings were left
alone. Evidence is in `evidence/large-feed-normal/`: raw feed, read-only database
backup, before-refresh identity map, result JSON and GUI screenshots.

## HD device-to-library pulls in the normal profile

The same running AppImage authenticated the attached Pavo Zune HD 16 GB and
read 839 music tracks, four videos, ten podcast episodes, zero photos and zero
audiobook parts. The HD video fallback correctly found the four objects despite
the ZMDB video count being zero. No device writes or deletions were performed;
the existing one-episode automatic queue and retention settings were preserved.

Actual GUI Save to Library actions extracted:

- Billie Eilish, `!!!!!!!`, MP3, 545,687 bytes.
- Avenged Sevenfold, `(D)eath`, MP3, 7,983,272 bytes.
- Bravest Warriors S01E01, S01E03, S01E04 and S01E05, H.264 Baseline/AAC MP4
  at 480×272, totaling 410,036,622 bytes.

All six files passed complete FFmpeg audio/video decoding with `-xerror`, with
zero decode errors. Music tags retained title, artist, album and track fields.
A second pull of `!!!!!!!` created a collision-safe `(2)` filename and matched
the first file's SHA256 exactly. The current prefer-FLAC setting excludes these
MP3 counterparts from the library index while preserving their downloaded files
and existing FLAC rows; this is the configured format resolver, not a failed
extraction. Music GUI playback of the downloaded counterparts was not asserted.

All four videos were indexed and matched to their series/season/episode. Each
was opened in the Zuuned player; live process file descriptors confirmed the
downloaded MP4 path rather than the original network MKV. Rendering was observed
for Time Slime. Escape returned to the library. Sound output was not independently
heard, and this does not establish physical handheld playback or resume.

Two follow-ups remain visible:

- Video imports coexist with the original episodes as duplicate cards. The
  season reports eight entries on the Zune for four actual device objects because
  both local representations receive the same presence badge. Local-copy display
  grouping and the count should account for this.
- Device podcasts/audiobooks currently offer browsing/removal, without Save to
  Library. The HD podcast browser correctly showed Moonbound and Nontendo with
  five episodes each and covers after loading. No photo or audiobook pull could
  be exercised because this HD has neither category populated.

Evidence is in `evidence/hd-pulls/`: bounded session log, baseline/results JSON,
per-file hashes/probes/full-decode results/library rows and GUI screenshots.
These are successful representative readback tests, not certification of every
track, all media categories or all device functionality. No rebuild was needed.
