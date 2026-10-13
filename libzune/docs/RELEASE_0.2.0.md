# libzune 0.2.0

Prerelease notes, prepared October 13, 2026. Publication and release artifacts
are tracked separately; this document does not certify a packaged build.

## New device APIs

- Podcast show inventory, creation and exact-feed reuse; prepared MP3 episode
  upload and lookup using stable source identity.
- Native podcast episode and audiobook part inventories, with explicit complete
  success or failure and matching ownership/free functions.
- Experimental audiobook part lookup and upload on Classic Zune 30 (Keel) and
  Zune HD (Pavo). Applications prepare MP3 parts and supply stable book identity.
- Podcast firmware bookmark/marked-played reads and bulk exact feed/source
  identity maps. These read-only APIs support application retention policies;
  unknown state never means played and reading state never authorizes deletion.
- Podcast cleanup outcomes distinguish confirmed deletion of a newly failed
  object from uncertain device state. Title-derived firmware filenames now
  accept the observed Classic punctuation behavior without loosening outgoing
  filename or identity checks.

The [API reference](API_REFERENCE.md) covers all 105 public functions. The
[integration guide](SPOKEN_MEDIA.md) explains identities, artwork placement,
return values, ownership and serialized device access. The
[source index](TOC.md) maps declarations to implementations.

## Evidence and limitations

Classic podcast audio and covers have user acceptance. Native two-part MP3
and cover readback passed on Classic and HD; Classic audiobook presentation was
accepted by the user. Firmware played-state evidence exists for both families.
The [research record](SPOKEN_MEDIA_RESEARCH.md) separates these observations
from software fixtures and records the real Classic podcast batch following the
filename correction.

This is not certification of all models, HD podcast playback/artwork, audiobook
resume, long-book chapter transitions, or every cleanup policy. RSS, downloads,
chapter extraction, conversion and user-facing retention remain application
responsibilities. DRM-protected books and video podcast sending are unsupported.
HD artist backgrounds remain deferred.

The shared-library build still lacks a versioned SONAME and pkg-config file;
this prerelease does not introduce a stable packaged ABI. Calls remain
synchronous and require serialized device ownership. Existing music, video,
photo and playlist APIs remain available.

## Build and authentication

See the [README](../README.md) and [authentication guide](BUILD_WITH_MTPZ.md).
Public source omits embedded authentication values and accepts an external
`~/.mtpz-data` file or a locally supplied header. Official Zuuned downloads
supply authentication as part of their separate trusted build process.
Original library code remains GPL-3.0-or-later, with separate third-party notices.
