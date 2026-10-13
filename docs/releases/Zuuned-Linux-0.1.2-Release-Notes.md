# Zuuned Linux 0.1.2

A new Linux testing build with the music-library improvements added after 0.1.1.

## Changes

- Improved artist identification using provider IDs and aliases.
- Album artwork matching considers edition information, track lists, disc positions and year. Confident album matches can use a general cover when exact-edition artwork is unavailable.
- Existing file artwork and manual artwork choices remain protected. Artwork discovery does not rename albums or add edition labels.
- Verified duplicate audio copies across imported folders share a single library presentation while retaining physical files and separate known album editions.
- Clearer artwork matching outcomes and saved manual provider selections.
- Setup background choices consistently offer the next step.

## Download

Download **Zuuned-Linux-0.1.2-x86_64.AppImage** and its `.sha256` file. The other assets are source, licensing and build records; they are not needed to run the app. See TESTER-README.md for launch instructions and exporting a debug report.

This is a development testing prerelease built on Debian 13, with Qt 6.8.2 and glibc 2.41. It targets x86_64 Linux with X11/XWayland and OpenGL. Older glibc distributions and ARM are not certified.

## Build source

Application baseline `426005d74320814f3341d7be8bfced52f854ed90` and libzune baseline `d29bf7a5492b481f3211fbaeb3c596a00db7c742`, both with working changes. The embedded build metadata reports both components as dirty; this is not presented as a clean tagged source build.

Use **Zuuned-Linux-0.1.2-Source.tar.gz** for this build's public application/libzune snapshot. GitHub's automatically generated source archives follow the tag target and are not the exact build snapshot. The populated authentication header is excluded from public source; the official executable retains embedded authentication. The source archive includes build instructions for supplying that input.

## Validation

- All 35 native software test groups passed.
- Packaging/export regressions and recursive inventory of 334 bundled ELF files passed.
- The exact AppImage passed an isolated Debian desktop rehearsal: fresh setup, media imports, artwork settings, desktop folder/open/save dialogs, diagnostics export, nested photo-album creation and restart persistence.
- Public source export and authentication-boundary checks passed.
- SHA256: `2d5b68056bdc7d8ce182f8da58b2b27cbb0fd41b8c742855b8eb16bbe583d3b3`.

## Limits

Artwork providers can have missing images, ambiguous releases or outages. Duplicate grouping verifies encoded audio copies; differently encoded versions can remain separate. Existing device duplicates are not automatically deleted. Container checks do not certify physical Zune transfers, hardware photo presentation, real GPU/audio playback or every Linux desktop. No new physical-device certification is claimed for this executable.

Original application and libzune code are GPL-3.0-or-later; third-party terms remain separate. See LICENSE.txt, COPYING.md and THIRD-PARTY-NOTICES.tar.gz.

Report issues: https://github.com/badgeromen/zuuned-linux/issues
Website: https://zuuned.com
