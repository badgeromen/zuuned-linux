# libzune

**Breach. Smuggle. Forge. Liberate.**

A C library for taking back your Zune. Authenticate over USB, smuggle music,
videos and photos, forge playlists, and keep abandoned hardware loud.

Native PTP/MTP/MTPZ. C99. No libmtp, Qt, GTK or GLib dependency. Applications
include [`zune.h`](include/zune.h) and link the static or shared library.

## The rebellion API

| Operation | Functions |
| --- | --- |
| Connect / disconnect | `zune_breach`, `zune_sever` |
| Send media | `zune_smuggle_track`, `zune_smuggle_movie`, `zune_smuggle_episode`, `zune_smuggle_photo` |
| Retrieve media | `zune_extract_track` and video/photo extraction functions |
| Create objects | `zune_forge_album`, `zune_forge_artist`, `zune_forge_playlist` |
| Set album artwork | `zune_brand` |
| Update objects | `zune_rewire_album`, `zune_rewire_playlist` |
| Delete media | `zune_purge_track`, `zune_purge_video`, `zune_purge_photo` |
| Read the device database | `zune_infiltrate` |
| Diagnose / recover | `zune_autopsy`, `zune_autopsy_name`, `zune_unjam` |

The [source index](docs/TOC.md) maps functions to their current declarations and
implementations. The header documents ownership, return values and callbacks.
Older research documents may describe superseded implementations; consult the
current header and index when integrating the library.

## Platforms and scope

- **Linux:** libusb backend. No custom kernel driver; USB permissions are required.
- **macOS:** IOKit backend communicating with a separate `ZuneUSBDriver` DriverKit
  extension. That extension is not included here. The Makefile expects Homebrew
  and configured FFmpeg include paths; cloning this repository alone does not
  provide the complete Mac driver setup.
- **Windows:** no Windows USB backend or build target in this repository.

Device identification covers Zune 30, second-generation/flash models and Zune HD.
Real Zune 30 authentication, library reads, transfers and deletion have been
exercised during development. Coverage varies by model and operation; identifying
a model does not certify every feature on that hardware.

Calls are synchronous. Serialize device operations and run them off the UI
thread. Release returned data with matching `zune_free_*` functions. Request
cancellation with `zune_abort` and let the active operation finish before teardown.
Do not terminate the process during a transfer: partial objects can remain on the
device. After writes, `zune_finalize` requests re-indexing where supported.

## Build

Linux dependencies: a C99 compiler, Make, pkg-config, libusb-1.0, libgcrypt,
libavformat and libavutil development files. The Makefile enables `USE_LIBAV`
for in-process metadata probing.

```bash
# Debian / Ubuntu
sudo apt install build-essential pkg-config git libusb-1.0-0-dev \
  libgcrypt20-dev libavformat-dev libavutil-dev ffmpeg

# Arch Linux
sudo pacman -S --needed base-devel pkgconf git libusb libgcrypt ffmpeg
```

```bash
git clone https://github.com/badgeromen/libzune.git
cd libzune
make
make zunetool
```

Build outputs: `libzune.a` and `libzune.so` on Linux, or `libzune.a` and
`libzune.dylib` on macOS. Optional installation: `make install PREFIX=/your/prefix`.
The shared-library target currently has no versioned SONAME or pkg-config file.

FFmpeg executables are used for thumbnail/photo preparation and legacy transcode
helpers. **The `zune_arm_*` helpers currently hard-code
`/opt/homebrew/bin/ffmpeg`; they are not portable Linux transcoders as written.**
Linux callers should prepare compatible media themselves. `zunetool smuggle`
sends the supplied file as-is. Zuuned's separate application transcode layer is
not included in this library.

## Authentication

Zune media access requires MTPZ authentication data. Public source can build
without embedded values. Supply a five-line `~/.mtpz-data` file, or create a
local `src/mtpz_keys.h` before compiling for built-in authentication.

Follow [Building with Zune authentication](docs/BUILD_WITH_MTPZ.md) for the upstream
download, header template, field order and rebuild instructions. External data
at `~/.mtpz-data` takes precedence over compiled defaults. A device handle and
basic device information alone do not prove authentication succeeded.

## Linux USB access

Install the supplied rules under both names. The early rule prevents generic MTP
probing; the later rule reapplies exclusions before desktop seat permissions.

```bash
sudo install -m 644 packaging/68-zuuned.rules /etc/udev/rules.d/68-zuuned.rules
sudo install -m 644 packaging/68-zuuned.rules /etc/udev/rules.d/72-zuuned.rules
sudo udevadm control --reload-rules
```

Unplug and reconnect the device. The rules grant access to the active desktop
user for the supported Zune USB IDs. Headless services need separately configured
permissions. Do not run two device managers against the same Zune simultaneously.

## Minimal example

Save as `example.c` in the repository root:

```c
#include "zune.h"
#include <stdio.h>

int main(void)
{
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) {
        const char *error = zune_get_error();
        fprintf(stderr, "Breach failed: %s\n", error ? error : "unknown error");
        return 1;
    }
    const char *name = zune_get_name(dev);
    printf("Connected: %s\n", name ? name : "Zune");
    printf("Battery: %u%%\n", (unsigned)zune_get_battery(dev));
    zune_sever(dev);
    return 0;
}
```

Link statically on Linux:

```bash
cc -std=c99 -Iinclude example.c ./libzune.a \
  $(pkg-config --libs libusb-1.0 libgcrypt libavformat libavutil) -o example
./example
```

## Test and diagnose

Start with read-only operations:

```bash
./zunetool info
./zunetool list
```

Transfer a file already encoded for the device:

```bash
./zunetool smuggle /path/to/track.mp3
```

The harness also provides `extract`, `playlists`, `refs`, `forge`, `purge`,
`audit`, `zmdbdump`, `torture` and `video-title-test`. Read
[`tools/zunetool.c`](tools/zunetool.c) before running mutation/stress commands.
`purge` deletes device content; stress tests perform repeated transfers.

Logs go to stderr with `[libzune]`, `[ptp]`, `[mtpz]` and backend tags. After a
failed device operation, inspect `zune_autopsy`: `0x200C` indicates full storage,
`0x2005` an unsupported operation, and `0x02FF` a transport failure. Report the
library revision, model, command and relevant logs, removing personal paths and
device identifiers. Do not include authentication data.

## License

Original libzune code is **GPL-3.0-or-later**. See [LICENSE](LICENSE) and
[COPYING.md](COPYING.md). Preserve separate notices for third-party material.

Your device. Your music. Your rules.
