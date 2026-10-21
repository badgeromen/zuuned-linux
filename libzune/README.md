# libzune

**Breach. Smuggle. Forge. Liberate.**

A C library for taking back your Zune. Authenticate over USB, smuggle music,
videos, photos, podcasts and audiobooks, forge playlists, and keep abandoned
hardware loud.

Native PTP/MTP/MTPZ. C99. No libmtp, Qt, GTK or GLib dependency. Applications
include [`zune.h`](include/zune.h) and link the static or shared library.

## The rebellion API

| Operation | Functions |
| --- | --- |
| Connect / disconnect | `zune_breach`, `zune_sever` |
| Send media | `zune_smuggle_track`, `zune_smuggle_video_named`, `zune_smuggle_photo`, `zune_smuggle_podcast_episode`, `zune_smuggle_audiobook_part` |
| Retrieve media | `zune_extract_track` and video/photo extraction functions |
| Create objects | `zune_forge_album`, `zune_forge_artist`, `zune_forge_playlist` |
| Podcast shows and episodes | `zune_get_podcast_series`, `zune_forge_podcast_series`, `zune_smuggle_podcast_episode`, `zune_find_podcast_episode` |
| Audiobook parts | `zune_get_audiobook_parts`, `zune_find_audiobook_part`, `zune_smuggle_audiobook_part` |
| Podcast listening state / source identity | `zune_get_podcast_states`, `zune_get_podcast_episode_sources` |
| Set album artwork | `zune_brand` |
| Update objects | `zune_rewire_album`, `zune_rewire_playlist` |
| Delete media | `zune_purge_track`, `zune_purge_video`, `zune_purge_photo` |
| Read the device database | `zune_infiltrate` |
| Diagnose / recover | `zune_autopsy`, `zune_autopsy_name`, `zune_unjam` |
| Library version | `zune_dossier` |

The [API reference](docs/API_REFERENCE.md) documents every public function,
including ownership and implementation limits. The [source index](docs/TOC.md)
maps functions to their current declarations and implementations. The header documents ownership, return values and callbacks.
Older research documents may describe superseded implementations; consult the
current header and index when integrating the library.

Podcast integration, artwork placement, exact identity, CLI testing and current
audiobook limits are in the [spoken-media guide](docs/SPOKEN_MEDIA.md). Podcast
audio and visible covers are confirmed on the tested Classic; experimental
native multipart audiobook transfer is now available on Zune 30, 80/120 and HD.
Podcast firmware played-state reads are available for those families too. Draco
(Zune 80/120) passed a small audiobook audio/art roundtrip and a read-only,
identity-matched podcast-state baseline. Completed-playback cleanup and corrected
Draco podcast playback/artwork acceptance remain hardware gates.
Audiobook resume, long-book transitions and other models remain separate gates.
Chaptered-file preparation belongs to the application, not libzune. See the
[0.3.0 release notes](docs/RELEASE_0.3.0.md) for this prerelease’s scope and
the [0.2.0 notes](docs/RELEASE_0.2.0.md) for the spoken-media release.

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

Build outputs: `libzune.a`, `libzune.so.0.3.0` (SONAME `libzune.so.0`, with
`libzune.so.0` and `libzune.so` links) and `libzune.pc` on Linux, or `libzune.a`
and `libzune.dylib` (install name `@rpath/libzune.dylib`) on macOS. Only the
functions declared in `zune.h` are exported; the library builds with
`-fvisibility=hidden`. Optional installation:
`make install PREFIX=/your/prefix`, which also installs `lib/pkgconfig/libzune.pc`
(`pkg-config --libs --static libzune` lists gcrypt, libusb and libav).

For standalone integrations, see the [buildable C examples](examples/README.md):
read-only spoken-media inventory, a prepared podcast episode send, and an
individual prepared audiobook part send. They use the public C API directly,
without Zuuned or Qt. Start with inventory before running a write example.

FFmpeg executables are used for thumbnail/photo preparation and the legacy
`zune_arm_*` / `zune_retag` transcode helpers. They run directly, never through a
shell, and are found in this order:

1. `ZUNE_FFMPEG`, `ZUNE_FFPROBE` or `ZUNE_SIPS` (macOS): a full path, or a name to
   look up on `PATH` instead (for example `ZUNE_FFMPEG=ffmpeg7`).
2. `PATH`. Empty entries are ignored; the current directory is never searched.
3. macOS only: `/opt/homebrew/bin`, then `/usr/local/bin`, for apps launched from
   Finder without the shell's `PATH`.

A missing tool fails with an error naming it and its override variable. The
transcode profiles are legacy helpers; `zunetool smuggle` sends the supplied file
as-is, and Zuuned's separate application transcode layer is not included here.

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
For podcast/audiobook integration examples and their usage, prefer
[examples/README.md](examples/README.md). The separate
[spoken-media guide](docs/SPOKEN_MEDIA.md#command-line-testing) explains the
bounded `podcast-test`, `podcast-send` and `audiobook-test` hardware tools.

Logs go to stderr as `[tag] message` lines (`[libzune]`, `[ptp]`, `[mtp]`,
`[mtpz]` and backend tags). `ZUNE_CHATTER=whisper|talk|shout|scream|silent`
sets the minimum level (default `talk`; `whisper` adds protocol detail such as
the device serial). Applications can route the same messages with
`zune_wiretap` and `zune_set_chatter`; `[mtpz]` lines still go to stderr
directly. Status functions return `ZUNE_OK` or `ZUNE_BLOWN` (the
[API reference](docs/API_REFERENCE.md#return-conventions) lists every
function's convention), and `zune_get_error` describes the most recent call
that can fail. After a failed device operation, inspect `zune_autopsy`: `0x200C` indicates full storage,
`0x2005` an unsupported operation, and `0x02FF` a transport failure. Report the
library revision, model, command and relevant logs, removing personal paths and
device identifiers. Do not include authentication data.

## License

Original libzune code is **GPL-3.0-or-later**. See [LICENSE](LICENSE) and
[COPYING.md](COPYING.md). Preserve separate notices for third-party material.

Your device. Your music. Your rules.

### Zune HD artist content

The native [HD artist-content C API](docs/HD_ARTWORK_API.md) serves Now Playing
backgrounds, a separate thumbnail, an ordered gallery and biography over USB.
No hosted server or Wi-Fi is required. The public-only `hd_artwork` example and
`zunetool hd-art-publish` expose validation and explicit serving. Existing-artist
cache refresh is not guaranteed; software tests and research hardware evidence
are documented separately. JPEG validation adds a libavcodec link dependency.
