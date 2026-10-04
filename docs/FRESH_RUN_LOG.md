# Fresh-install rehearsal — 2026-09-02

> Historical development record. Dates, paths, plans and results below describe
> that investigation, not the current downloadable release. Start with the
> [documentation guide](README.md) for current build and tester instructions.

The dress rehearsal: dev box stripped to stranger conditions (legacy
`99-zune.rules` and the `zune_usb` DKMS module removed, `~/.mtpz-data`
retired to backup), rebuilt AppImage run under a completely empty HOME.
Zune 120 plugged in throughout. Orson drives; every wince is logged.

## Environment findings (before the app even ran)

- **Desktop distros grant Zune USB access out of the box.** libmtp's
  `69-libmtp.rules` tags the device `ID_MTP_DEVICE=1`, and systemd's
  `70-uaccess.rules` grants the seated user access to MTP devices
  generically. The EACCES/no-permission scenario belongs to minimal
  and headless setups — on mainstream desktops the *actual* first-
  contact risk is gvfs's MTP mounter probing the device (which our
  packaged rule's `MTP_NO_PROBE` suppresses; without any rule the app
  won the race on this box, but that's a race).
- **Embedded MTPZ keys carried the whole rehearsal**: fresh HOME, no
  key file anywhere on the box, full handshake + enumeration. The
  tester crypto path is real.
- **The Zune's own screen shows "syncing" on every session open** and
  runs a visible re-index after session close. Testers WILL ask "is it
  syncing?!" — the answer is no, but the app never tells them.

## Pain points

1. **Welcome screen has no brand.** Plain sans "welcome to zuuned" —
   no marker wordmark, no L15 mark. The app's strongest identity is
   absent from its first impression.
2. **A plugged-in Zune is unacknowledged during onboarding.** The
   device is right there, enumerated and breached — the welcome flow
   should say "Zune 120 detected ✓" instead of pretending it isn't.
3. **Onboarding copy omits mixtapes/playlists** — the showpiece
   feature isn't in the pitch ("sync music, videos & photos").
4. **The portal file picker is unbrandable and can be blinding.**
   xdg-desktop-portal follows the SYSTEM GTK theme — stock/light
   setups get a searing white dialog inside a dark-only app, and we
   cannot theme it from our side. Fix: an in-app dark folder browser
   (house style), portal demoted or dropped.
5. **Manual path entry loses to the picker button.** Even the author
   reached for the shiny button first. If the manual field is the
   escape hatch for network mounts and ugly pickers, it needs equal
   billing.
6. **Onboarding folder types omit ANIME.** Settings offers
   music/movies/tv/anime/photos; onboarding offers only four. The
   anime pivot + folder-truth machinery exists — onboarding just
   never got the fifth button.
7. **Device "sync theater" needs a one-liner.** Related to the
   environment finding: first-connect is the moment to toast/show
   "the zune shows its sync screen whenever a computer is connected —
   nothing is being transferred."

8. **Scrollbars were ghosts.** Basic-style bars only appear WHILE
   scrolling and are too thin to grab. Fixed during the rehearsal:
   ZuneScrollBar (always visible when content overflows, fat handle,
   hover growth, click-track jumps) applied to all 25 scroll surfaces.
9. **First play on a small window teaches nothing.** The ribbon is
   retired under 1280px, so hitting play produced no visible change
   for a new user. Fixed during the rehearsal: playback starting from
   stopped on a narrow window auto-opens the now-playing island once
   — the user sees what happened and learns the surface. Track
   advances never re-open it.
10. **The dev box resurrects its kernel module.** zune_usb auto-loads
    on every replug via USB id matching — rmmod alone can't produce
    stranger conditions. RESOLVED during the run: module fully removed
    (dkms remove + source deleted), full backup in ~/zune-rule-backup/
    (source + built .ko + restore recipe). The box is now permanently
    in stranger conditions: no rule, no module, no key file.

11. **Query cleaner misses obvious junk** (Needs Match evidence):
    "Anastasia ENG ITA Shiv@ 1997" (language pairs + release-group
    tails survive), "Jumanji RM4K 1995" (RM4K), "Obi Wan Kenobi The
    Patterson Cut 2 1 Surround" (channel layouts 2.1/5.1/7.1 become
    "2 1"; "Surround"/"HDR"/"SDR" survive), "The Godfather Edit"
    ("Edit-1" tails). Each junk token poisons the TMDB query —
    strip: language pairs, RM4K/remaster tags, x.y channel layouts,
    Surround/HDR/SDR, trailing -GroupName@ / (GroupName) release
    tags. Disc-rip stems (C1_t00/D1_t02) are honestly unparseable —
    those correctly land in Needs Match.
12. **Library must keep itself** (requested during run): resume
    matching after app close — already true (matcher drains
    interrupted backlog at launch) — plus launch rescan and live
    folder watching for new files. (Built during rehearsal: launch
    sweep + root watcher + 5-min incremental sweep.)

13. **Manual fix-match locked out until the first sweep finishes.**
    A new user who spots a wrong match and tries to fix it during the
    initial lookup churn gets rebuffed — teaching them their manual
    input loses to the robot. Manual fixes must apply instantly and
    win, mid-sweep included (user_edited stickiness already exists —
    the gating is UI-side).
14. **Music Videos + Other must be first-class, end to end.** The
    Zune's own Videos menu has both sections; our pipeline half-knows
    them (metagenres 0x23/0x21, category values) but nothing user-
    facing exists. Scope (per Orson): folder types for them in
    ONBOARDING and Settings, library visibility, queueing TO the zune
    under the right metagenre, and save-to-library FROM those device
    sections. Full round trip, not just a classify action.

15. **UI never learns the background scan found videos/photos —
    the counts are a stale read, not a missing scan. CONFIRMED twice.**
    Symptom: onboarding "scan complete" shows 0 movies / 0 tv / 0
    photos, and Settings shows "0 videos in library", after adding
    folders full of real media. FALSE ALARM on the scanner: a WAL-
    aware snapshot of the fresh-run DB holds 181 tracks (music still
    probing), **6015 videos (272 movie + 5743 tv), 945 photos** — the
    video/photo phases DID run and the matcher is actively matching
    them. The bug is READ VISIBILITY / UI REFRESH: the scan writes on
    background threads, but reloadVideos()/the QML model refresh isn't
    fired when the video+photo scan commits, so the UI (and the
    onboarding count query) keeps showing 0 until something forces a
    reload — a manual rescan, an app restart, or navigating in.
    (It fooled the diagnosis too: a plain `?mode=ro` count read 0
    while the DB held 6015 — same class of stale-snapshot read the
    UI is doing.) Fix: emit the video/photo model refresh when the
    background scan commits rows, and have the onboarding summary
    read live counts, not a one-shot snapshot. NOT a scanner fix.

16. **Dual-format album folders double every track (design gap, not
    a dup bug).** Real find: Hollow Knight Silksong OST ships FLAC +
    MP3 of all 53 tracks in sibling subfolders (HK_SS_OST_FLAC_V2 /
    _MP3_V2). Scanner correctly found all 106 files; they carry
    identical album/title/track metadata so they stack as visual
    doubles. NOT a dedup failure — genuinely distinct files. But two
    real costs: every album view doubles, and a sync would push BOTH
    copies to the Zune (redundant, wastes space, device can't
    distinguish). Consideration: dedup by (artist/album/title/track)
    with a format preference (e.g. FLAC>MP3 for library, transcode
    picks source; or user-set), OR a format badge so it reads as
    intentional. Lean: dedup-with-preference — the sync-both waste is
    the real cost. Synthetic libraries never have this; only a real
    dual-format rip surfaces it.

17. **Removing a watch folder orphans its media in the DB.**
    CONFIRMED in code: removeWatchFolder does only `DELETE FROM
    watch_folders WHERE id=?` — it never purges the tracks/videos/
    photos that came from that folder. So a "removed" folder's whole
    library lingers, unplayable-linked and confusing. Design miss:
    releasing a directory must cascade-purge its rows (WHERE filepath
    LIKE '<root>/%'), then refresh the models. High-priority data
    correctness bug.
18. **New artists/content don't appear after a rescan — SAME root
    as #15.** addWatchFolder already calls rescan(), but the scan
    writes to the DB on a background thread and the MUSIC models
    (artists/albums/songs) never reload, so new content is invisible
    until forced. User's workaround (recategorize the folder) worked
    only because setWatchFolderType ALSO calls rescan() and something
    in that path finally refreshed. Unifies with #15: the QML models
    (music AND video AND photo) must reload when a background scan
    commits rows. One fix covers both.
19. **No visual cue that the library is scanning.** When folders are
    added/removed or new files land, the scan runs invisibly — no
    spinner, no "scanning N files…", no completion toast. The user
    can't tell if anything is happening (which is WHY #15/#18 read as
    "broken" — silent success is indistinguishable from failure).
    Add a persistent scan-activity indicator (library chrome) tied to
    the scanning() state, plus a "found N new items" toast on finish.
20. **Settings menus need a redesign — they "truly suck".** Reported
    duplication and poor structure across the tabs (appearance/media/
    sync/about). Wants a real IA pass, not a patch. Design-first:
    mock the new Settings on the board before rebuilding.

21. **Deleting from the Zune doesn't free up the app's space number —
    resync blocked until reconnect. CONFIRMED from the log.** Sequence:
    1.1 GB free → delete (finalize: 0x9201 refused 0x2005 as expected,
    CleanDataStore 0x9108 OK, device re-indexes) → resync attempt hits
    "capacity gate: 1 refused, 1.1 GB free" TWICE → reconnect → 3.47 GB
    free (the delete freed ~2.4 GB all along). Root: purgeItems never
    calls refreshStorage(), so the capacity gate blocks new syncs
    against a stale "full" figure. Fix (mirror of noteBytesWritten):
    optimistically credit freed bytes back to m_freeGB immediately for
    instant smoothness, THEN refreshStorage() after the purge's
    finalize/CleanDataStore for the authoritative number. HD reconnect
    also showed a self-healed OpenSession wedge (0x02FF → auto-reset →
    clean) — not a bug, but noted.

22. **Big transcode-then-send reads as a freeze — no feedback
    reaches the user.** Queued a 77-min 1080p movie (A Goofy Movie).
    The app transcoded it (111,730 frames, ~pure CPU, ZERO device
    I/O — Zune screen idle the whole time) then started a 1.57 GB USB
    send. From the user's seat: "nothing is happening, the zune shows
    nothing, did it freeze?" NOT a bug (verified: 83% CPU, healthy,
    send in flight) — a feedback blind spot. The transcode phase
    especially is invisible: no "transcoding A Goofy Movie… 96%",
    no per-phase status, so long jobs are indistinguishable from a
    hang. Fix: surface transcode progress (the frame counter already
    exists in the log — pipe it to the UI) and a clear "transcoding /
    sending / N of M" state on the sync surface. Ties to UX-5 (toast
    replacement) and the sync-report work.

23. **Titles show the raw filename + extension; should show the clean
    title like the real Zune did.** GROUND TRUTH (libzune
    WIRE_CAPTURE_FINDINGS.md, captured from the official client's own
    episode upload): the real Zune sent Name (0xDC44) = "Let You Down"
    as the DISPLAY title — no extension, no SxxExx, no series — while
    the filename + SxxExx lived only in ObjectFileName (0xDC07), and
    Series/Season/Episode were dedicated fields (0xDA9A / 0xDAB5 /
    0xDAB6). The device composed its display from those. Our library
    view instead renders the raw filename ("A Goofy Movie.mp4",
    "…- S01E01 - ….mp4"). Fix, matching the wire truth: movies show
    the clean parsed title (no ext, no release-junk — see #11);
    series show "S01E01 · <episode Name>", number-first. We already
    have episodeTitle/series/season/episode/tmdbTitle in the DB — it's
    a display-field swap, not new data. Confirms Orson's instinct
    exactly; Microsoft did it this way.

24. **Video detail pages have NO drag-to-queue.** CONFIRMED: zero
    DragGhost / drag.target in VideoSeriesDetail or VideoMovieDetail.
    Music got drag-everywhere (albums, artists, tracks, playlists,
    mixtapes) in UX-2, but the video detail pages never received it —
    so you can't drag a specific episode, a whole season, or a movie
    onto the device panel. Parity gap: bring the album-page drag
    pattern to episode rows (single episode), the season pill (whole
    season), and the movie hero (the movie).
25. **Photos can't be dragged to queue either.** Same UX-2 drag
    parity gap on the photo grids (library + device). Individual
    photos and albums should drag onto the device like tracks do.
26. **The photo "+" opens fullscreen instead of queueing.** The ＋ IS
    wired to queuePhotos in code, but the click lands on the
    underlying fullscreen tap-target instead — a hit-order/z problem:
    the whole-tile fullscreen MouseArea eats the tap meant for the ＋
    overlay. Users reach for +, get a lightbox, conclude add is
    broken. Fix: ＋ (and ⌕) must sit above the fullscreen MouseArea
    in hit order, or the tile splits its hit regions.
27. **Zune HD shows only a generic "syncing" screen — no % — during
    a sync.** Observation, likely a device-firmware limit, not our
    bug: we send the 0x922A sync-notify (itemIndex/totalItems) that
    drives the DEVICE's own progress display, and it shows a percent
    on Keel (30) but the HD (Pavo) just parks on a plain sync screen.
    CONFIRMED as a firmware-family split (not our bug): the Zune 120
    (Draco, family 0x03) DOES render our 0x922A percentage on-device;
    the HD (Pavo, 0x06) does not — same notify code, different
    firmware. So classic/HDD Zunes (30/80/120) are covered by the
    device's own % display; the HD alone shows nothing. Our 0x922A is
    working correctly. Reinforces #22: in-app progress is REQUIRED
    because it's the HD family's ONLY feedback path (classic users get
    it from the device).

28. **The udev fix-it instruction references a file AppImage testers
    don't have.** Both the C3 in-app card and the README say
    `sudo cp packaging/45-zune.rules /etc/udev/rules.d/` — but an
    AppImage user has a single binary, no repo, no packaging/ dir. So
    the fallback breaks for exactly the audience it exists for
    (AppImage on a minimal distro lacking stock libmtp uaccess rules).
    Fix: make it self-contained — the card already holds the rule
    text, so emit a `sudo tee /etc/udev/rules.d/45-zune.rules <<EOF …
    EOF && sudo udevadm control --reload` with the two rule lines
    inline. Zero file dependency, works from a bare AppImage. AUR path
    is fine (pacman installs the file). Update C3 card + README + the
    TESTER_INSTALL doc.

29. **Our udev rule loses a numbering race to libmtp — MTP_NO_PROBE
    is dead.** Installed 45-zune.rules and replugged; the device tags
    came up `ID_MTP_DEVICE=1 uaccess uaccess`. Our `TAG+="uaccess"`
    applied (the doubled tag), but our `ENV{ID_MTP_DEVICE}=""` and
    `ENV{MTP_NO_PROBE}="1"` did NOT stick. Cause: udev runs rules in
    filename-number order — ours is 45, libmtp is 69-libmtp.rules, so
    ours fires first (blanks ID_MTP_DEVICE) and libmtp runs AFTER and
    re-sets it to 1. The gvfs-suppression half of our rule is inert on
    any box with libmtp (i.e. most). Permissions half is fine (device
    connects). Fix: rename to 99-zune.rules (>69) so it runs after
    libmtp and the blanking/NO_PROBE win. Update the packaged rule,
    the desktop-install path, the fix-it card command, README, and the
    pkexec button target. Bonus: this makes the app stop losing the
    gvfs race it currently wins only by force-detaching.
    (C3 card itself behaved correctly — warning cleared once rule
    present AND device connected.)

30. **Queue count shows GROUP count, not item count — photos never
    bump it.** CONFIRMED: queueCount = queueRepeater.count, and the
    repeater is over groupSummary(). groupKeyFor collapses all photos
    of one album into a single "p\x01<album>" group (same as tracks
    grouping by album). So adding 5 photos from one folder makes ONE
    group row → the "queue (N)" label goes 0→1 and then never moves no
    matter how many more you add from that album. User reads it as
    "photos aren't counted." Fix: the count label should reflect total
    ITEMS (or "N items · M groups"), not group count. Affects tracks
    too, but photos make it obvious since they're added one at a time.
31. **Solo photo sync appears to queue the WRONG image.** The DATA is
    per-photo correct — photosInAlbum gives each photo its own url,
    photoSyncItem reads p.url — so the synced FILE is probably right.
    Most likely culprit: the single-photo ＋ calls queuePhotos([parent
    .modelData]) and `parent.modelData` isn't resolving to the clicked
    cell (QML parent-chain: the SplitAddButton's parent may be an
    intermediate Item without modelData, so it grabs a stale/other
    photo). Alt: the queue ROW thumbnail resolves to a shared album
    representative. NEEDS a live click-trace to pin which — but the
    ＋'s `parent.modelData` reach is the prime suspect and a known
    fragile pattern. Verify the ＋ binds the cell's own modelData
    explicitly, not via parent.

32. **Manual-override is missing almost everywhere — "smart by
    default" has no backstop.** Capability audit:
    - Audio track tags: EXISTS (EditTrackSheet + editTrackMetadata,
      writes ID3+DB) — but low discoverability (right-click only).
    - Video series/season/episode/title: backend exists
      (updateVideoSeries) but only reachable via the TMDB fix-match
      sheet = "pick a different AUTO-match," not free manual entry.
    - Album / artist cover — MANUAL SET: MISSING. No setArt/pickArt
      anywhere.
    - Movie / series poster — MANUAL SET: MISSING (TMDB re-match only,
      can't supply your own file).
    - Free-text movie/series metadata (title/description/genre):
      MISSING (no sheet; locked to TMDB).
    BIG one: the custom-artwork flow (file-picker → cache → user
    poster) was called "THE HEART OF UX-3" at the very start, got
    DESIGNED on the mockboard ("click the sleeve to change art"), and
    was NEVER BUILT. Principle to honor: smart auto by default, manual
    override as a backup EVERYWHERE — tags, artwork, poster, video
    metadata. Needs: (1) a universal "set custom image" flow (file
    pick → cache → sticky override the matcher won't clobber, reusing
    the user_edited stickiness), (2) a free-edit sheet for video
    metadata, (3) surface track Edit Info more prominently, (4) let it
    reach device-side items too, not just library.

33. **HD 720p profile → Zune HD synced correctly (NOT a bug —
    earlier note retracted).** Verified from the log: reconnected to
    Pavo (Zune HD) THEN sent the video, so the HD 720p profile matched
    the HD device exactly. Transcode honored the explicit profile
    (0xB982 H264, no WMV fallback, no auto-override) → 720p H264 to an
    HD = correct. WIN, not a defect. (Scribe error corrected: I first
    anchored to a stale 120 connect line; Orson flagged it — the
    device at send time was the HD.) Residual CONSIDERATION only: does
    the profile picker guard at all if someone DOES pick an HD profile
    while on a classic 120? Worth a mismatch warning as defense, but
    this run was correctly matched — no bug observed.
34. **No AMD/Intel hardware transcode — and no hardware ENCODE on
    Linux at all.** Confirmed in transcode/libav_transcode.c: Linux
    video ENCODE is software libx264 for everyone (the slow half — the
    675s Goofy Movie transcode went here). The "hwDecode" setting is
    NVDEC/CUDA — NVIDIA-only AND decode-only (speeds source read, not
    output write). AMD + Intel users get ZERO acceleration, both
    directions. There is no VAAPI path. So there's no "AMD setting" to
    toggle — it doesn't exist. Real work: (1) VAAPI decode for AMD/
    Intel (mirror the NVDEC decode path), (2) hardware ENCODE
    (VAAPI/NVENC) — the biggest transcode-speed win available, and the
    thing that makes big-movie syncs bearable on non-NVIDIA boxes.

## What went right (worth saying)

- One file, chmod +x, run: the AppImage started clean on a bare HOME.
- Embedded keys: zero-setup MTPZ, hardware-verified again.
- Onboarding appeared exactly as designed; skip/pager present.
- Device breach, enumeration, thumbnails — all before any setup.
- Needs Match caught everything it couldn't parse instead of
  guessing — the roll-up is doing its job.
- **Zune HD (Pavo) full connect was flawless** — embedded-key MTPZ
  handshake, 359 tracks / 87 photos / 49 videos (HD MTP fallback),
  battery + storage, all clean. Second device model proven this run.
- **★ BOTH device families synced end-to-end on the stranger box.**
  Zune HD (Pavo, 0x06): 1.5 GB movie — transcode → send → finalize →
  re-index. Zune 120 (Draco, 0x03): tracks at 10-11 MB/s + album/
  artist forge (1 ok, 0 failed) → finalize. No udev rule (until the
  ordering-bug experiment), no kernel module, no key file, embedded
  MTPZ, AppImage. THE CORE WORKS. Every one of the 32 findings is
  polish / UX / edge-case — not one is "the fundamentals are broken."
  Classic 120 wire ~11 MB/s vs HD ~6.7 (firmware/controller, not us).
- **ZERO USB setup on a mainstream desktop.** With OUR module and
  rule both removed, a plugged-in Zune came up `crw-rw-r--+` (ACL) via
  stock rules: libmtp's 69-libmtp.rules tags it ID_MTP_DEVICE, systemd's
  70-uaccess.rules grants the seated user access. The app breached and
  enumerated with no sudo, no rule, no module. C3's fix-it card is a
  fallback for minimal/headless boxes, NOT the common path. The
  packaged rule's real value is MTP_NO_PROBE (gvfs race), and even that
  the app usually wins on its own by force-detaching the interface.

## Status

Rehearsal in progress. Fixed live during the run (before scribe
discipline kicked in): 8, 9, 12. Queued for the pre-Round-2 fix
list: 1-7, 11, 13, 14, 15.
