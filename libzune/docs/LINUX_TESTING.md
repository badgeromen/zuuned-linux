# Testing libzune on Linux

The macOS app needs the DEXT + Xcode. libzune itself is portable C with a
libusb backend, so a Linux box with a Zune plugged in is the fastest rig
for exercising the protocol layer — no driver, no code signing, no app.
This is the recommended way to validate the wire-protocol fixes.

## Setup

Use the current [README build instructions](../README.md#build),
[USB permission rules](../README.md#linux-usb-access), and
[MTPZ authentication guide](BUILD_WITH_MTPZ.md).

```bash
git clone https://github.com/badgeromen/libzune.git
cd libzune
make
make zunetool
```

The Linux Makefile enables in-process FFmpeg metadata probing. Install the
libavformat/libavutil development packages as well as libusb and libgcrypt.
The harness sends supplied media as-is; it does not convert arbitrary codecs.

## Test commands

```bash
./zunetool info                 # connect, print device + storage, disconnect
./zunetool list                 # list tracks currently on the device
./zunetool send song.mp3        # smuggle one track (default metadata)
./zunetool send song.mp3 "Title" "Artist" "Album" "Genre"
./zunetool purge 12345          # delete an item by id
./zunetool torture song.mp3     # send the SAME file 20x, then clean up
```

Every failure prints the **autopsy**: the PTP response code and its name
(`StoreFull 0x200C`, `AccessDenied 0x200F`, `TransportError 0x02FF`), so a
failure tells you *why*, not just that it happened.

## Validating the ZLP fix specifically

The split-mode ZLP bug was **size-triggered and deterministic**: a file
whose payload is an exact multiple of 512 bytes used to hang every time.

1. Find or make a file whose size mod 512 == 0 (`truncate -s $((N*512)) t.bin`
   won't be a valid MP3 — use a real track that happens to land on the
   boundary, or just run `torture` on a normal file; over 20 sends the old
   code failed on any 512-multiple and cascaded).
2. On a historical pre-fix revision, `torture` shows a
   run of failures with `TransportError 0x02FF` and everything after the
   first failure also failing (session desync).
3. On the corrected implementation, `torture` should report `20 ok, 0 failed`.

That before/after is the direct proof the fix works, on real hardware,
without needing macOS at all.

## Capturing protocol traffic

To compare against the Windows client captures
(`docs/WIRE_CAPTURE_FINDINGS.md`), record while zunetool runs:

```bash
sudo modprobe usbmon
sudo tshark -i usbmon0 -Y 'usb.transfer_type == 3' -w /tmp/zunetool.pcapng
# ...run ./zunetool send... in another terminal, then Ctrl-C
```

Then decode with the same reconstruction approach used on the Windows
captures (OUT = irp_info 0x00, IN = 0x01; honor split headers).
