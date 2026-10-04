# macOS Zune Quirks — Everything That's Different from Linux

> **Documentation status (2026-10-04):** Historical macOS testing notes, retained as evidence for that platform/session. The DriverKit extension lives outside this repository. Consult README.md and API_REFERENCE.md for the current library interface and teardown rules.

## Overview

Getting a Zune to work on macOS required solving problems that don't exist on Linux. This document catalogs every macOS-specific issue we discovered and how we solved it.

## USB Interface Conflicts

### Problem
macOS automatically claims Zune USB interfaces via system daemons:
- `PTPCamera` — Apple's PTP camera handler
- `ptpcamerad` — PTP camera daemon
- `AMPDeviceDiscoveryAgent` — Apple Music Protocol device scanner
- `AMPLibraryAgent` — Apple Music library agent

These daemons grab the USB interface before libmtp can claim it, causing "device busy" errors.

### Solution
Kill all competing daemons before connecting:
```c
system("killall -9 PTPCamera 2>/dev/null");
system("killall -9 ptpcamerad 2>/dev/null");
system("killall -9 AMPDeviceDiscoveryAgent 2>/dev/null");
system("killall -9 AMPLibraryAgent 2>/dev/null");
usleep(3000000);  // 3 seconds for macOS to release interfaces
```

Linux equivalent only needs to stop gvfs-mtp (1.5s wait).

## libgcrypt Initialization

### Problem
libgcrypt (used by libmtp for MTPZ crypto) prints `"Libgcrypt warning: missing initialization"` on macOS if not initialized before use.

### Solution
Call before `LIBMTP_Init()`:
```c
gcry_check_version(NULL);
gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
```

`gcry_control()` is variadic — can't be called from Swift directly. Wrapped in a C helper function in the bridging header.

## USB Bulk Transfer Stalls

### Problem
macOS libusb has documented issues with bulk transfers:
- Transfers can stall indefinitely without returning timeout errors
- `libusb_bulk_transfer()` hangs forever on `LIBUSB_ERROR_PIPE`
- Partial transfers on macOS don't return transferred byte counts correctly

This causes the sync to freeze mid-transfer with no error and no recovery.

### Solution
USB stall recovery with retry:
```c
int zune_clear_stall(ZuneDevice *dev) {
    libusb_clear_halt(handle, inep);   // clear IN endpoint
    libusb_clear_halt(handle, outep);  // clear OUT endpoint
}
```

All send functions wrapped with 3-attempt retry:
1. Try send
2. If fail: `zune_clear_stall()` + delete orphan handle + wait 500ms
3. Retry up to 3 times

## Tracks Not Playing — Audio Codec Properties

### Problem
Tracks sent from macOS showed correct metadata but the Zune said "cannot be played." Same tracks played fine from Linux.

### Root Cause
`LIBMTP_new_track_t()` initializes `samplerate=0, nochannels=0, bitrate=0, wavecodec=0`. The `LIBMTP_Update_Track_Metadata()` function passes these zeros through `adjust_u32()` which picks the first enumerated value from the device's supported list. The first enumerated codec might NOT be the correct one for the file being sent.

### Solution
Set audio properties explicitly before sending:
```c
track->samplerate = 44100;
track->nochannels = 2;
track->wavecodec = 0x0055;   // MPEG Layer 3 (or 0x0161 for WMA)
track->bitrate = 320000;
track->bitratetype = 1;      // CBR (or 0 for WMA VBR)
```

## Photos Unviewable — Zune Re-index

### Problem
Photos transferred to the Zune showed "0 bytes" or "item cannot be played." The JPEG data was on the device (downloadable via MTP) but the Zune's photo viewer couldn't display them.

### Root Cause
The Zune's photo viewer needs Width (0xDC87) and Height (0xDC88) properties, which libmtp never sets during `send_file_object_info()`. The Zune populates these from JPEG headers during its own **re-indexing cycle**, which only happens when the device restarts.

On Linux, the Zune automatically restarts after the MTP session closes. On macOS, users were unplugging the cable (abrupt disconnect) instead of using the eject button (clean session close).

### Solution
Force a clean USB interface release after sync by calling `LIBMTP_Release_Device()` (which sends `ptp_closesession`). This triggers the Zune to restart and re-index, populating Width/Height from JPEG headers.

The app shows "sync complete — disconnect your Zune to apply changes" after sync.

## PNG Photos Rejected

### Problem
Sending PNG screenshots to the Zune fails with `PTP Invalid Code Format (0x2016)`.

### Root Cause
The Zune firmware doesn't support PNG format (0x380B). It only accepts JPEG for photos.

### Solution
All photos preprocessed before sending:
```
sips -s format jpeg -s formatOptions 90 -Z 480 input.png --out /tmp/output.jpg
```

Converts any format to baseline JPEG, resizes to max 480px (Zune screen max), preserves aspect ratio.

## ID3v2.4 Tags Ignored

### Problem
MP3 files with ID3v2.4 tags show "Unknown Artist" / "Unknown Album" on the Zune.

### Root Cause
The Zune firmware only reads ID3v2.3 tags. ID3v2.4 is completely ignored.

### Solution
Retag MP3s before sending (stream copy, no re-encoding):
```
ffmpeg -i input.mp3 -c copy -map_metadata 0 -id3v2_version 3 -write_id3v1 1 -y output.mp3
```

## Vendor Operations All Fail

### Problem
All `LIBMTP_Custom_Operation()` calls return -1 on macOS:
- 0x9201 (ReportAddedDeletedItems) — fails
- 0x9202 (ReportAcquiredItems) — fails
- 0x9171 (CloseMediaSession) — fails
- 0x9204-0x9243 (all unknown ops) — fail

Only 0x9108 (CleanDataStore) succeeds.

### Impact
None — the vendor operations are decorative. The Zune re-indexes on USB disconnect regardless.

## `Set_Object_Raw` on Photos = Corruption

### Problem
Calling `LIBMTP_Set_Object_u32_Raw()` to set Width/Height on photo objects permanently corrupted ALL photos on the device. Photos became unviewable with no recovery except deleting them.

### Lesson
**Never write MTP properties to photo objects post-send.** The Zune's photo metadata is read-only from the firmware's perspective — it populates properties from JPEG headers during re-index. Writing to them corrupts the internal state.

## IOKit USB Auto-Detection

macOS uses IOKit for USB hotplug detection instead of Linux's udev:
```c
IOServiceAddMatchingNotification(port, kIOFirstMatchNotification,
    matchDict, callback, context, &iterator);
```

Match by vendor (0x045E) and product (0x0710 / 0x063E) IDs. Fires instantly when Zune is plugged in — no polling needed.

## ZMDB Works Inside PTP Session

Despite sending raw (non-PTP) bulk packets, the ZMDB read (opcode 0x1792) works while a PTP session is active. The Zune handles non-PTP traffic on the same endpoints without disrupting the session. This was confirmed by both zune-explorer (JavaScript) and our C implementation.

**Critical:** Must use the actual endpoint numbers from libmtp's PTP_USB struct, NOT hardcoded values. The endpoints are discovered during USB interface claim.
