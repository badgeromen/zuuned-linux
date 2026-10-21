# Build something with libzune

These small C99 programs use only the public `zune.h` API. They are integration
examples, separate from `zunetool`'s protocol research and hardware stress tests.
They use synchronous calls on one thread, release every owned result and stop
on failed lookups. There is no Qt, RSS client or application database involved.

## Build and check without a device

Install the dependencies from the [library README](../README.md#build), then run
from the libzune checkout:

```sh
make examples
./examples/bin/device_info --help
./examples/bin/spoken_inventory --help
./examples/bin/podcast_send --help
./examples/bin/audiobook_send --help
bash tests/run-examples.sh
```

`make examples` links the examples against this checkout's `libzune.a`, using the
same platform dependencies as the library. Outputs go in ignored `examples/bin/`;
`make clean` removes them. Building, `--help` and the mocked test gate never open
USB. Linux compilation and software fixtures are checked; macOS still needs the
external DriverKit setup documented in the README and is not certified by these
checks.

Before running a real example, configure [authentication](../docs/BUILD_WITH_MTPZ.md)
and [USB access](../README.md#linux-usb-access), and close other Zune managers.
A connected handle does not prove authentication. Public builds can compile
without a private header; spoken queries fail explicitly if authentication is
unavailable. Do not share raw authentication logs. Transfer failures print device
serials and object IDs for local recovery; review logs before sharing them.

## Read information and spoken media

```sh
./examples/bin/device_info
./examples/bin/spoken_inventory
```

[device_info.c](device_info.c) reads basic name/model/battery information.
[spoken_inventory.c](spoken_inventory.c) reads shows, episodes, audiobook parts,
firmware listening state and exact episode source identities. Books are grouped
by parent ID and ordered by part number. Episodes resolve missing series names
through the real series ID. These examples do not write media or finalize.

The inventory example distinguishes a failed state read from an empty result.
Its printed `live-correlation` checks item/series/parent IDs plus title/duration.
That is only a diagnostic: it does not establish which copies an application
owns or authorize deletion. A real retention policy must also match its persisted
feed/source identities and keep preferences, revalidate the target before removal,
and treat unavailable or conflicting state as unknown. `marked_played` is the
firmware flag, not proof of uninterrupted completion. No example deletes content.

## Send a podcast episode

[podcast_send.c](podcast_send.c) accepts a **local, already prepared MP3** and an
optional prepared JPEG. Duration is positive milliseconds from that MP3.

```sh
./examples/bin/podcast_send --send /path/to/episode.mp3 \
  'https://example.org/feed.rss' 'https://example.org/episode-one.mp3' \
  'Example show' 'Example author' 'Episode one' 60000 /path/to/cover.jpg
```

Replace the example URLs with the episode's real identity. The library does not
fetch either URL. Keep the feed URL and original device source URL stable across
retries; changing enclosure download URLs should not silently change device
identity. Persist episode GUIDs and resolve source collisions in your application.

The program queries before writing. When a show is missing, it creates a parent
folder named with this invocation’s timestamp/process ID and forges a series,
then reads back its current parent and name. This example naming is not a
persisted application folder policy. Existing
show names are preserved. Even a found episode goes through the sender's stricter
metadata/byte-size reuse validation. The optional cover goes on the **series ID**.
Reuse is not a byte-for-byte content hash comparison.

The explicit `--send` action can retain folders, shows and episodes. On failure,
the example prints status and known IDs, then stops without retrying or deleting.
`ZUNE_PODCAST_FAILED_CLEANED` proves only that the newly failed object's deletion
was confirmed; a parent folder or show may remain. Other failures can leave
uncertain state even with ID zero. Persist device serial, stable identity and
known IDs in a real application's recovery journal.

## Send audiobook parts

[audiobook_send.c](audiobook_send.c) sends **one prepared MP3 part**. It does not
parse an M4B, extract chapters or perform whole-book capacity admission. Those
are caller responsibilities; Zuuned implements them separately.

```sh
# Define and retain an immutable identity manifest for the whole book, including
# its edition and complete ordered source hashes. Hash it ONCE and persist it.
book_key=$(sha256sum /path/to/book-identity-manifest.txt | cut -d ' ' -f 1)
./examples/bin/audiobook_send --send /path/to/chapter-01.mp3 \
  "$book_key" 'Example book' 'Example author' 'Opening' 1 60000 /path/to/cover.jpg
./examples/bin/audiobook_send --send /path/to/chapter-02.mp3 \
  "$book_key" 'Example book' 'Example author' 'Chapter two' 2 120000 /path/to/cover.jpg
```

The key must be exactly 64 lowercase hexadecimal characters. All parts of the
same book use the same persisted key; distinct editions need distinct identities.
Use a reviewed part order from 1 through 65535. Author may be an empty string
(`''`), but the pointer must not be NULL. Titles and positive durations are
required. Different identities using the same book title may be refused to avoid
firmware merging; choose distinct book names for those editions.

The program performs live lookup and lets the sender validate reuse, including
metadata and byte size. Optional artwork goes on **each part ID**, never a music
album object. Failed sends preserve returned IDs for inspection. Earlier successful
parts and newly created folders can remain; there is no rollback or automatic retry.

## Media preparation and completion

The examples check regular file size and numeric inputs before connecting. They
do not establish that arbitrary input bytes are valid MP3 audio. Inspect/encode
compatible media in the caller; renaming M4B/AAC files to `.mp3` is not conversion.
Keep prepared files and input strings unchanged throughout each synchronous send.
The example cover loader accepts JPEG signatures up to 1 MiB as a local example
limit, not a firmware limit; prepare appropriate dimensions/encoding separately.

Successful sends and cover failures are reported separately. A cover failure
leaves the successful media transfer intact. `zune_finalize` is requested after
successful sends, including a cover failure. Its current return value is best
effort and cannot certify firmware reindexing. Disconnect normally and check the
device. Hardware success, chapter transitions, playback and resume are separate
from compiling or running software fixtures. See [supported behavior and evidence](../docs/SPOKEN_MEDIA.md).

## HD artist content

`hd_artwork` is a public-header-only client for backgrounds, thumbnail, gallery
and biography delivery. `--validate` is entirely offline; explicit `--serve`
opens hardware and may create a fresh artist. See the [integration guide](../docs/HD_ARTWORK_API.md)
for exact arguments, ownership, callback ordering and partial/cache results.
It is also available as `zunetool hd-art-publish`. It never deletes or retries.
