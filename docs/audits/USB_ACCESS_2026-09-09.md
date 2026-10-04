# USB access without ZUUNED rules — 2026-09-09

## Verified result

The local `f7239b6` app (libzune `d29bf7a`, Qt 6.11.2) authenticated and
read the attached Draco 120 GB Zune as the normal desktop user after all three
custom Zune udev rules were disabled. The read found 704 tracks, 195 videos and
80 photos. No transfer was started for this check.

This is a host permission and connection rehearsal, not a new AppImage gate or
a physical unplug/replug test. The existing device received a synthetic `add`
event after its previously granted user ACL was explicitly removed.

## What grants access

The installed stock files explain the complete chain:

1. `/usr/lib/udev/hwdb.d/69-libmtp.hwdb` matches `usb:v045Ep0710*` and
   supplies `ID_MEDIA_PLAYER=1` and `ID_MTP_DEVICE=1`. A direct
   `systemd-hwdb query` for the attached device returned both values.
2. `50-udev-default.rules` imports USB hardware database properties.
3. `70-uaccess.rules` tags USB devices with `ID_MEDIA_PLAYER` as `uaccess`.
4. `73-seat-late.rules` invokes the `uaccess` builtin. The active local seat
   belongs to UID 1000, and the USB node receives a named read/write user ACL.

The files are supplied by libmtp 1.1.23-1 and systemd 261.2-1. No custom
`zune_usb` module was loaded. This access follows the installed desktop's
normal permissions; it does not demonstrate an Omarchy permission bypass.

## Rehearsal and retained evidence

The app was already closed, its last log ended with successful CleanDataStore
and USB close, and the in-flight marker was absent. The approved operation:

- Moved `45-zune.rules`, `68-zuuned.rules` and `72-zuuned.rules` out of
  `/etc/udev/rules.d` into `/var/tmp/zuuned-udev-retest.GcIMvt`.
- Reloaded rules, cleared the existing ACL on `/dev/bus/usb/001/003`, and
  verified UID 1000 could no longer write that node.
- Triggered and settled an `add` event for the attached Zune only. UID 1000's
  write access returned. `getfacl` again showed the named user with `rw-`.
- Confirmed `ID_MEDIA_PLAYER=1`, `ID_MTP_DEVICE=1` and `uaccess` were present,
  while the custom `MTP_NO_PROBE` property was absent.
- Opened the native app through Omarchy and verified authentication and device
  library read in `/tmp/zuuned-no-custom-rules-app.log`.

The root-owned backup directory also holds before/cleared/after ACLs and
before/after udev properties. Raw logs and device identifiers stay local.
The custom rules remain disabled for the user's test. Installing the supported
rules through Settings recreates the canonical 68/72 pair; do not restore the
obsolete 45 rule as a replacement for that pair.

## UI implication

`DeviceService::udevRuleOk()` checks the installed ZUUNED rule files through
`UdevSetup::installed()`. It does not measure effective USB access. The panel
can therefore say that a permission rule is needed while stock rules already
grant access, including while a present device is disconnected/re-indexing.

A later behavior/copy fix should distinguish missing ZUUNED setup from an
actual access-denied failure. Our rules also suppress competing MTP probing;
successful access without them does not verify protection against another
desktop MTP client. No application behavior was changed during this audit.
