# Operation Clean Getaway — the hardening & ship plan

> Historical development record. Dates, paths, plans and results below describe
> that investigation, not the current downloadable release. Start with the
> [documentation guide](README.md) for current build and tester instructions.

Goal: a stranger installs Zuuned, plugs in whatever Zune they own, and
it just works — and when it can't, it says why in a sentence, not a
stack trace. This plan covers every known gap between "works
beautifully for Orson" and "survives the public."

House rules apply throughout: full-scale fixes over shortcuts, every
hardware claim gated on a real device, docs updated as gates pass.

---

## Phase A — Data survives

The library DB is the only home of mixtapes and watched-states. It must
be un-losable.

- **A1 · Persist the sync queue.** New table in the library DB; queue
  restored on launch (entries re-verified: file still exists, device
  still matches). Kills the "restart forgot my queue" class entirely.
- **A2 · Schema migrations.** `PRAGMA user_version` + an ordered
  migration list run at open. Downgrade guard: newer-schema DB + older
  app refuses politely instead of corrupting.
- **A3 · Backups + escape hatch.** Rotate a timestamped DB copy before
  any migration; manual "export library data" in Settings. Playlists
  additionally exportable/importable as M3U so mixtapes are never
  hostage to our schema.

**Gate:** kill -9 mid-queue-edit, upgrade across a schema bump, restore
from backup — zero data loss in all three.

## Phase B — Device truth (the models we haven't earned yet)

Validated deeply: Zune 30 (Keel), Zune HD (Pavo). The 120 log already
shows real bugs.

- **B1 · Zune 120/80 metadata pass.** Fix the garbage photo/video size
  math (`627189298496528.2 MB summed`, 182/185 videos at 0 bytes),
  handle `get_object_info` 0x2005/0x2002 refusals with per-model
  fallbacks, clamp storage attribution to physical capacity. Wireshark
  capture against the Windows client on the 120 if parsing stays
  ambiguous — captures are ground truth here.
- **B2 · Orphan sweep at connect.** Detect partial/zero-size objects
  (the truncated-send firmware-crash case) and offer one-click purge +
  CleanDataStore. Turns our worst failure mode into a toast.
- **B3 · Multi-device queue semantics.** Stamp the queue with the
  device it was built against; re-run dedup for ALL entry types at
  sync start; visible note when a different Zune is plugged in.

**Gate:** 120 shows sane storage/library; a deliberately killed send
recovers via the sweep; queue built on the HD syncs correctly to the 120.

## Phase C — Citizen of the desktop

- **C1 · MPRIS2.** D-Bus interface off PlayerService: play/pause/next/
  prev/seek/metadata/artUrl. Media keys, sound applets, and desktop
  now-playing widgets all light up. Biggest polish-per-effort win left.
- **C2 · Single-instance guard.** Lock on the DB (or DBus name);
  second launch focuses the first. Two instances sharing the USB claim
  is a data-loss hazard today.
- **C3 · Permission-failure UX.** libusb EACCES → an in-app fix-it
  card ("install the udev rule → replug") with a copyable command, not
  a stderr line. Packaging installs `45-zune.rules` and reloads rules
  automatically.

**Gate:** media keys work on Hyprland + GNOME; double-launch focuses;
fresh box with no udev rule self-explains the fix.

## Phase D — When things go wrong

- **D1 · Sync report.** Per-entry outcomes already exist in the queue
  model — persist the last run and give it a small "last sync" view
  (feeds the planned UX-5 toast replacement). Autopsy codes surfaced
  with one-line human translations (0x200C "the zune is full", 0x2005
  "the device refused — don't retry").
- **D2 · First-run health checks.** Onboarding verifies: udev rule
  present, TMDB key valid (if entered), watch folders reachable
  (kubeplex mounts!), disk space for transcode temps. Each failure is a
  fix-it card, not a mystery later.
- **D3 · No-TMDB-key mode verified.** Video section must degrade to
  folder-truth cleanly: no lookup spinners that never resolve, no
  empty-poster crashes. Test the full flow keyless.

**Gate:** a tester with no API key and a broken mount gets a working
music app and two actionable cards, zero errors.

## Phase E — Ship mechanics

- **E1 · TMDB key decision.** Either guided user-supplied keys
  (signup link + paste field, already have the field) or a tiny proxy
  with our key. Recommendation: user-supplied for v1 — zero infra,
  D3 makes keyless acceptable.
- **E2 · MTPZ material posture.** Decide and document how the MTPZ
  certs/keys ship (bundled vs fetched vs derived at first run) before
  the AUR upload makes the decision for us.
- **E3 · Packaging.** AUR (source) + AppImage (portable). udev rule
  install + reload in both. About tab: version, libzune commit, device
  compatibility list.
- **E4 · QML regression harness.** Offscreen screenshot runs of every
  page at 3 widths, diffed in CI; qmllint as a gate. Today's
  pivot-over-searchbar class of bug becomes machine-caught.
- **E5 · Scale pass.** ◑ First round run 2026-09-02 (synthetic 40k
  tracks): scanner is FAST (40k fresh in <20s; incremental instant) —
  but the QML track-snapshot Instantiator materialized a QObject per
  row, freezing the UI and leaking to OOM on every fresh mass insert.
  Replaced with a C++ rowsSnapshot() call: flat ~1.1GB during scan,
  ~815MB idle at 40k. Still open: 200-playlist mixtape wall, video
  scale (synthetic files must beat the min-size sample filter), and
  trimming the ~800MB idle floor.

**Gate:** clean Arch VM + clean Ubuntu VM: install → onboard → sync a
playlist to a real Zune, no terminal ever opened.

## Phase F — Family reunification (libzune + the mac)

The Linux-validated line is 19 commits ahead of master; the mac app
pins master and is missing the ZLP rule, playlist forge, retry triage,
1MB chunks — some of its open bugs are already fixed upstream.

- **F1 · Merge `fix/wire-protocol` → `master`.** ✅ DONE 2026-09-02 —
  PR #3 merged; both zuuned-linux branches repinned to merged master,
  `.gitmodules` follows `master` again.
- **F2 · Repoint the mac app** to the merged master; run its known-bug
  list against the new line (artist display, finalize behavior).
- **F3 · Purge the mac submodule's tracked `obj/` artifacts** — the
  standing discipline violation that keeps biting branch switches.
- **F4 · App-layer backports to the mac:** LAME superframe chunking,
  zero-byte abstract-object commit, magic-byte sniffing, sync
  pipelining/perf, and (eventually) the UX-3 pages.

**Gate:** both apps build against the same libzune commit; mac syncs an
album + playlist on real hardware.

## Phase G — Rebellion polish

The C API is fully in character (breach/sever/smuggle/forge/purge/
extract) and the app code speaks it internally. The USER-facing seams
are mixed:

- **G1 · zunetool verbs.** `send` → `smuggle`, `mkplaylist` → `forge`
  (old verbs kept as quiet aliases). `purge`, `torture`, `audit`,
  `extract` are already in character. Usage text gets one line of
  attitude.
- **G2 · Language pass on user-visible strings.** Logs keep their
  `[prefix]` discipline; UI copy stays plain-spoken (queue/sync/eject
  are clearer than in-jokes for strangers) — the rebellion lives in
  the CLI, the API, and the docs, not in error messages a newcomer has
  to decode.

**Gate:** `zunetool smuggle track.mp3` works; README/docs use the
canonical verbs.

---

## Order of battle

1. **A1, C2, B2** — the data-loss and corruption hazards (fast, high value)
2. **B1** — the 120 pass (needs the device on the desk)
3. **C1, C3, D1–D3** — desktop citizenship + error surfaces
4. **F1–F3** — libzune merge + mac repoint (unblocks mac backports any time)
5. **E1–E5** — ship mechanics, ending in the clean-VM gate
6. **A2/A3, G1/G2** — slot in wherever a session has slack

Status 2026-09-02 EOD: A1-A3 ✅ · B1 ✅ (honest counts-only on
non-HD; batched ObjectSize sweep = future real-sizes enhancement) ·
B2 ✅ · B3 ✅ · C1-C3 ✅ · D1-D3 ✅ (D3: 356 failed lookups absorbed,
flat memory, folder-truth intact) · E1 ✅ (bundled key + override) ·
E2 ✅ (embedded MTPZ hardware-verified) · E3 ◑ (AppImage + AUR + icons
shipped; About tab + compat AppImage open) · E4 ✅ (lint gate + 3-width
capture harness; CI wiring later) · E5 ✅ (Instantiator OOM fixed;
40k tracks + 1.8k videos + 200 playlists flat at 1.4GB; idle-RAM trim
noted) · F1 ✅ · F3 ✅ (master tracks no artifacts) · F2/F4 await the
mac machine · G1 ✅ · G2 ✅.

Open tails: About tab, compat AppImage on demand, mac repoint +
backports, ObjectSize sweep, idle-RAM trim, CI wiring for E4.
